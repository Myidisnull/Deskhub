#include "deskhubp/system/AuthProof.h"

#include "deskhubp/system/Random.h"

namespace deskhubp {

AuthSalt NewAuthSalt() {
    AuthSalt salt{};
    RandomBytes(salt.data(), salt.size());
    return salt;
}

}
