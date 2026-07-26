// =============================================================================
// Credentials.swift — mật khẩu và token thiết bị của từng host, lưu trong Keychain
//                     (GĐ10). Đối ứng client/android/.../ui/Credentials.kt.
//
// NHIỆM VỤ
//   Ba thứ, và cả ba đều phải sống qua lần tắt app:
//     1. clientId    — danh tính ỔN ĐỊNH của bản cài này. Host khoá danh sách
//                      "Trusted devices" theo nó, nên nó mà đổi thì deviceToken vô
//                      dụng và người dùng phải gõ mật khẩu mãi mãi.
//     2. password    — mật khẩu đã lưu cho MỘT host ("Save the password for this
//                      machine" ở màn Settings của bản thiết kế).
//     3. deviceToken — token host cấp sau lần đáp đúng đầu tiên; chìa nó ra là khỏi
//                      hỏi mật khẩu. Chỉ đi trên dây ĐÚNG MỘT LẦN.
//
// VÌ SAO KEYCHAIN CHỨ KHÔNG PHẢI UserDefaults
//   Recents.swift lưu địa chỉ máy bằng UserDefaults, và điều đó ổn — địa chỉ IP
//   không phải bí mật. Mật khẩu thì khác: UserDefaults là một file plist trong hộp
//   cát của app, nằm nguyên trong bản sao lưu iTunes/Finder không mã hoá và đọc được
//   bằng nhiều công cụ. Keychain được mã hoá bằng khoá gắn với Secure Enclave.
//
// kSecAttrAccessibleWhenUnlockedThisDeviceOnly
//   "WhenUnlocked" — không đọc được khi máy đang khoá. "ThisDeviceOnly" — KHÔNG đi
//   theo bản sao lưu sang máy khác. Cái thứ hai là chủ ý: token thiết bị định danh
//   ĐÚNG CÁI MÁY NÀY với host, nên khôi phục sang máy mới mà vẫn vào được là sai với
//   chính ý nghĩa của danh sách "Trusted devices".
//
// LIÊN QUAN: DeskhubClient.swift (dh_start/dh_submit_password),
//            deskhub/auth/PasswordAuth.h, docs/04-protocol.md §7b
// =============================================================================
import Foundation
import LocalAuthentication
import Security
import UIKit // UIDevice.current.name — tên hiện ở "Trusted devices" phía host

/// Bí mật đã lưu cho một host. Trường rỗng = chưa có.
struct HostCredential: Identifiable, Sendable {
    let address: String
    var password: String = ""
    var deviceToken: Data = .init()

    var id: String { address }
    var hasPassword: Bool { !password.isEmpty }
    var hasToken: Bool { !deviceToken.isEmpty }
}

enum Credentials {
    private static let service = "com.deskhub.credentials"
    private static let clientIdKey = "deskhub.clientId"
    private static let indexKey = "deskhub.credentialIndex"

    // MARK: — Danh tính máy

    /// Danh tính ổn định của bản cài này, sinh một lần rồi giữ mãi.
    ///
    /// Khác 0 là bắt buộc: tầng C++ hiểu 0 là "caller quên truyền" và lùi về đồng hồ,
    /// lúc đó tính năng nhớ thiết bị im lặng ngừng hoạt động.
    static var clientId: UInt32 {
        if let data = read(account: clientIdKey), data.count == 4 {
            let v = data.withUnsafeBytes { $0.loadUnaligned(as: UInt32.self) }
            if v != 0 { return v }
        }
        // SecRandomCopyBytes chứ không phải arc4random: giá trị này đi vào proof xác
        // thực và là khoá tra thiết bị tin cậy phía host.
        var v: UInt32 = 0
        _ = withUnsafeMutableBytes(of: &v) { buf in
            SecRandomCopyBytes(kSecRandomDefault, 4, buf.baseAddress!)
        }
        if v == 0 { v = 1 }
        write(account: clientIdKey, data: withUnsafeBytes(of: v) { Data($0) })
        return v
    }

    /// Tên hiện ở danh sách "Trusted devices" phía host ("iPhone 15 Pro").
    static var deviceName: String {
        UIDevice.current.name
    }

    // MARK: — Bí mật theo host

    static func forAddress(_ address: String) -> HostCredential? {
        let key = normalize(address)
        guard index().contains(key) else { return nil }
        let pw = read(account: "pw:\(key)").flatMap { String(data: $0, encoding: .utf8) } ?? ""
        let tok = read(account: "tok:\(key)") ?? Data()
        if pw.isEmpty, tok.isEmpty { return nil }
        return HostCredential(address: key, password: pw, deviceToken: tok)
    }

    static var all: [HostCredential] {
        index().sorted().compactMap { forAddress($0) }
    }

    static func savePassword(_ password: String, for address: String) {
        let key = normalize(address)
        guard !key.isEmpty else { return }
        write(account: "pw:\(key)", data: Data(password.utf8))
        addToIndex(key)
    }

    static func saveToken(_ token: Data, for address: String) {
        let key = normalize(address)
        guard !key.isEmpty, !token.isEmpty else { return }
        write(account: "tok:\(key)", data: token)
        addToIndex(key)
    }

    /// Quên một host — ứng với nút Forget ở màn "Saved passwords".
    ///
    /// Xoá CẢ mật khẩu lẫn token: giữ token lại thì "quên mật khẩu" vẫn vào được, mà
    /// người dùng bấm Forget chính vì không muốn máy này vào được nữa.
    static func forget(_ address: String) {
        let key = normalize(address)
        delete(account: "pw:\(key)")
        delete(account: "tok:\(key)")
        var idx = index()
        idx.remove(key)
        writeIndex(idx)
    }

    static func forgetAll() {
        for key in index() {
            delete(account: "pw:\(key)")
            delete(account: "tok:\(key)")
        }
        writeIndex([])
    }

    // MARK: — Face ID

    /// Ô "Unlock with Face ID before connecting". Mặc định TẮT: bật một cửa sinh trắc
    /// học mà người dùng không tự chọn sẽ khiến họ tưởng app hỏng.
    static var biometricEnabled: Bool {
        get { UserDefaults.standard.bool(forKey: "deskhub.biometricUnlock") }
        set { UserDefaults.standard.set(newValue, forKey: "deskhub.biometricUnlock") }
    }

    /// Máy này có Face ID / Touch ID dùng được không — UI ẩn ô đó đi nếu không.
    static var biometricAvailable: Bool {
        LAContext().canEvaluatePolicy(.deviceOwnerAuthenticationWithBiometrics, error: nil)
    }

    /// Hỏi Face ID trước khi dùng mật khẩu đã lưu. Trả true nếu được phép đi tiếp.
    ///
    /// Tắt ô đó, hoặc máy không có sinh trắc học → true ngay, không hỏi gì. Đây là
    /// một cửa Ở TẦNG GIAO DIỆN: bí mật vốn đã được Keychain mã hoá, cửa này chỉ chặn
    /// người cầm máy đang mở khoá của bạn bấm Connect.
    static func unlock(reason: String) async -> Bool {
        guard biometricEnabled, biometricAvailable else { return true }
        let ctx = LAContext()
        return await withCheckedContinuation { cont in
            ctx.evaluatePolicy(
                .deviceOwnerAuthenticationWithBiometrics,
                localizedReason: reason
            ) { ok, _ in cont.resume(returning: ok) }
        }
    }

    // MARK: — Keychain

    // Địa chỉ là KHOÁ TRA, nên phải chuẩn hoá: "192.168.1.7" và "192.168.1.7 " gõ ra
    // hai mục khác nhau thì người dùng lưu mật khẩu xong vẫn bị hỏi lại.
    private static func normalize(_ address: String) -> String {
        address.trimmingCharacters(in: .whitespacesAndNewlines).lowercased()
    }

    private static func query(account: String) -> [String: Any] {
        [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
    }

    private static func read(account: String) -> Data? {
        var q = query(account: account)
        q[kSecReturnData as String] = true
        q[kSecMatchLimit as String] = kSecMatchLimitOne
        var out: CFTypeRef?
        guard SecItemCopyMatching(q as CFDictionary, &out) == errSecSuccess else { return nil }
        return out as? Data
    }

    private static func write(account: String, data: Data) {
        // Xoá rồi thêm, thay vì SecItemUpdate: gọn hơn, và không có đường "mục đã tồn
        // tại nhưng thuộc tính accessible khác" phải xử lý riêng.
        delete(account: account)
        var q = query(account: account)
        q[kSecValueData as String] = data
        q[kSecAttrAccessible as String] = kSecAttrAccessibleWhenUnlockedThisDeviceOnly
        SecItemAdd(q as CFDictionary, nil)
    }

    private static func delete(account: String) {
        SecItemDelete(query(account: account) as CFDictionary)
    }

    // Keychain không liệt kê theo tiền tố account được một cách gọn gàng, nên giữ một
    // chỉ mục địa chỉ riêng. Nó KHÔNG phải bí mật (chỉ là danh sách địa chỉ, thứ
    // Recents.swift vốn đã lưu công khai) nên để ở UserDefaults là đủ.
    private static func index() -> Set<String> {
        Set(UserDefaults.standard.stringArray(forKey: indexKey) ?? [])
    }

    private static func addToIndex(_ key: String) {
        var idx = index()
        idx.insert(key)
        writeIndex(idx)
    }

    private static func writeIndex(_ set: Set<String>) {
        UserDefaults.standard.set(Array(set), forKey: indexKey)
    }
}
