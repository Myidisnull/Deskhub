#include "deskhubp/system/AuthProof.h"

#include <openssl/bio.h>
#include <openssl/curve25519.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/mem.h>
#include <openssl/pem.h>
#include <openssl/rand.h>
#include <openssl/sha.h>
#include <openssl/x509.h>

#include <cstring>

#include "deskhubp/diag/Log.h"

namespace deskhubp {

namespace {

constexpr std::string_view kHostName = "deskhub-host";
constexpr std::string_view kClientName = "deskhub-client";

struct BioDeleter {
    void operator()(BIO* bio) const {
        BIO_free(bio);
    }
};

struct X509Deleter {
    void operator()(X509* cert) const {
        X509_free(cert);
    }
};

struct PkeyDeleter {
    void operator()(EVP_PKEY* key) const {
        EVP_PKEY_free(key);
    }
};

struct MdCtxDeleter {
    void operator()(EVP_MD_CTX* ctx) const {
        EVP_MD_CTX_free(ctx);
    }
};

using BioPtr = std::unique_ptr<BIO, BioDeleter>;
using X509Ptr = std::unique_ptr<X509, X509Deleter>;
using PkeyPtr = std::unique_ptr<EVP_PKEY, PkeyDeleter>;
using MdCtxPtr = std::unique_ptr<EVP_MD_CTX, MdCtxDeleter>;

X509Ptr CertFromPem(std::string_view pem) {
    if (pem.empty()) return nullptr;
    BioPtr bio(BIO_new_mem_buf(pem.data(), int(pem.size())));
    if (!bio) return nullptr;
    return X509Ptr(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

PkeyPtr PrivateKeyFromPem(std::string_view pem) {
    if (pem.empty()) return nullptr;
    BioPtr bio(BIO_new_mem_buf(pem.data(), int(pem.size())));
    if (!bio) return nullptr;
    return PkeyPtr(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

PkeyPtr PublicKeyFromSpki(std::span<const uint8_t> spkiDer) {
    if (spkiDer.empty()) return nullptr;
    const uint8_t* at = spkiDer.data();
    X509_PUBKEY* pubkey = d2i_X509_PUBKEY(nullptr, &at, long(spkiDer.size()));
    if (pubkey == nullptr) return nullptr;
    PkeyPtr key(X509_PUBKEY_get(pubkey));
    X509_PUBKEY_free(pubkey);
    return key;
}

}

std::vector<uint8_t> IdentityPublicKey(const HostIdentity& identity) {
    const X509Ptr cert = CertFromPem(identity.certPem);
    if (!cert) return {};
    X509_PUBKEY* pubkey = X509_get_X509_PUBKEY(cert.get());
    if (pubkey == nullptr) return {};
    uint8_t* der = nullptr;
    const int len = i2d_X509_PUBKEY(pubkey, &der);
    if (len <= 0 || der == nullptr) return {};
    std::vector<uint8_t> out(der, der + len);
    OPENSSL_free(der);
    return out;
}

std::optional<deskhub::Fingerprint> FingerprintOfPublicKey(std::span<const uint8_t> spkiDer) {
    if (spkiDer.empty()) return std::nullopt;
    if (!PublicKeyFromSpki(spkiDer)) return std::nullopt;
    deskhub::Fingerprint fp;
    SHA256(spkiDer.data(), spkiDer.size(), fp.bytes.data());
    return fp;
}

std::vector<uint8_t> SignWithIdentity(const HostIdentity& identity,
    std::span<const uint8_t> data) {
    const PkeyPtr key = PrivateKeyFromPem(identity.keyPem);
    if (!key) return {};
    const MdCtxPtr ctx(EVP_MD_CTX_new());
    if (!ctx) return {};
    if (EVP_DigestSignInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1) return {};

    size_t len = 0;
    if (EVP_DigestSign(ctx.get(), nullptr, &len, data.data(), data.size()) != 1) return {};
    std::vector<uint8_t> signature(len);
    if (EVP_DigestSign(ctx.get(), signature.data(), &len, data.data(), data.size()) != 1)
        return {};
    signature.resize(len);
    return signature;
}

bool VerifySignature(std::span<const uint8_t> spkiDer, std::span<const uint8_t> data,
    std::span<const uint8_t> signature) {
    if (signature.empty()) return false;
    const PkeyPtr key = PublicKeyFromSpki(spkiDer);
    if (!key) return false;
    const MdCtxPtr ctx(EVP_MD_CTX_new());
    if (!ctx) return false;
    if (EVP_DigestVerifyInit(ctx.get(), nullptr, EVP_sha256(), nullptr, key.get()) != 1)
        return false;
    return EVP_DigestVerify(ctx.get(), signature.data(), signature.size(), data.data(),
               data.size()) == 1;
}

AuthSalt NewAuthSalt() {
    AuthSalt salt{};
    RAND_bytes(salt.data(), salt.size());
    return salt;
}

AuthNonce NewAuthNonce() {
    AuthNonce nonce{};
    RAND_bytes(nonce.data(), nonce.size());
    return nonce;
}

PasscodeVerifier MakePasscodeVerifier(const AuthSalt& salt, std::string_view passcode) {
    SHA256_CTX ctx;
    SHA256_Init(&ctx);
    SHA256_Update(&ctx, salt.data(), salt.size());
    SHA256_Update(&ctx, passcode.data(), passcode.size());
    PasscodeVerifier out{};
    SHA256_Final(out.data(), &ctx);
    return out;
}

AuthMac ComputeAuthMac(std::span<const uint8_t> key, std::span<const uint8_t> data) {
    AuthMac mac{};
    unsigned int len = 0;
    HMAC(EVP_sha256(), key.data(), key.size(), data.data(), data.size(), mac.data(), &len);
    return mac;
}

bool MacsMatch(const AuthMac& a, const AuthMac& b) {
    return CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0;
}

std::vector<uint8_t> AuthTranscript(std::string_view label, const AuthNonce& nonce,
    const deskhub::Fingerprint& hostFingerprint, std::span<const uint8_t> extra) {
    std::vector<uint8_t> out;
    out.reserve(label.size() + 1 + nonce.size() + hostFingerprint.bytes.size() + extra.size());
    out.insert(out.end(), label.begin(), label.end());
    out.push_back(0);
    out.insert(out.end(), nonce.begin(), nonce.end());
    out.insert(out.end(), hostFingerprint.bytes.begin(), hostFingerprint.bytes.end());
    out.insert(out.end(), extra.begin(), extra.end());
    return out;
}

struct Spake2Session::Impl {
    SPAKE2_CTX* ctx = nullptr;
    bool generated = false;

    ~Impl() {
        if (ctx != nullptr) SPAKE2_CTX_free(ctx);
    }
};

Spake2Session::Spake2Session() : impl_(std::make_unique<Impl>()) {
}

Spake2Session::~Spake2Session() = default;

bool Spake2Session::Start(bool asHost, const PasscodeVerifier& verifier,
    std::vector<uint8_t>& outMessage) {
    if (impl_->ctx != nullptr) return false;
    const std::string_view mine = asHost ? kHostName : kClientName;
    const std::string_view theirs = asHost ? kClientName : kHostName;
    impl_->ctx = SPAKE2_CTX_new(asHost ? spake2_role_alice : spake2_role_bob,
        reinterpret_cast<const uint8_t*>(mine.data()), mine.size(),
        reinterpret_cast<const uint8_t*>(theirs.data()), theirs.size());
    if (impl_->ctx == nullptr) return false;

    outMessage.assign(SPAKE2_MAX_MSG_SIZE, 0);
    size_t written = 0;
    if (SPAKE2_generate_msg(impl_->ctx, outMessage.data(), &written, outMessage.size(),
            verifier.data(), verifier.size()) != 1) {
        outMessage.clear();
        return false;
    }
    outMessage.resize(written);
    impl_->generated = true;
    return true;
}

bool Spake2Session::Finish(std::span<const uint8_t> peerMessage, PasscodeVerifier& outKey) {
    if (impl_->ctx == nullptr || !impl_->generated || peerMessage.empty()) return false;
    uint8_t key[SPAKE2_MAX_KEY_SIZE];
    size_t written = 0;
    if (SPAKE2_process_msg(impl_->ctx, key, &written, sizeof(key), peerMessage.data(),
            peerMessage.size()) != 1)
        return false;
    if (written < outKey.size()) return false;
    std::memcpy(outKey.data(), key, outKey.size());
    impl_->generated = false;
    return true;
}

}
