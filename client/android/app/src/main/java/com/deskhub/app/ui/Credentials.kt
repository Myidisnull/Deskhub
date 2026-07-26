// =============================================================================
// Credentials.kt — mật khẩu và token thiết bị của từng host, mã hoá khi lưu (GĐ10).
//                  Đối ứng client/ios/app/swift/Credentials.swift.
//
// NHIỆM VỤ
//   Ba thứ, và cả ba đều phải sống qua lần tắt app:
//     1. clientId  — danh tính ỔN ĐỊNH của bản cài này. Host khoá danh sách "thiết bị
//                    tin cậy" theo nó, nên nó mà đổi thì deviceToken vô dụng và người
//                    dùng phải gõ mật khẩu mãi mãi.
//     2. password  — mật khẩu người dùng đã lưu cho MỘT host ("Save the password for
//                    this machine" ở màn Settings).
//     3. deviceToken — token host cấp sau lần đáp đúng đầu tiên; chìa nó ra là khỏi
//                    hỏi mật khẩu. Chỉ đi trên dây ĐÚNG MỘT LẦN.
//
// VÌ SAO KEYSTORE + AES/GCM CHỨ KHÔNG PHẢI SharedPreferences THƯỜNG
//   Recents.kt lưu địa chỉ máy bằng SharedPreferences trần, và điều đó ổn — địa chỉ
//   IP không phải bí mật. Mật khẩu thì khác: file prefs nằm trong thư mục app, đọc
//   được bằng adb trên máy đã root hoặc qua một bản backup. Khoá AES sinh trong
//   Android Keystore KHÔNG BAO GIỜ rời khỏi vùng phần cứng bảo mật — kể cả đọc được
//   file, không có khoá thì đó chỉ là byte rác.
//
// VÌ SAO KHÔNG DÙNG androidx.security:security-crypto
//   EncryptedSharedPreferences đúng là thứ sinh ra cho việc này, nhưng Google đã
//   ĐÁNH DẤU LỖI THỜI nó (2024). Thêm một dependency mới mà nó đã hết được bảo trì
//   thì chỉ là dời khoản nợ sang chỗ khác. Keystore + AES/GCM là API nền tảng, không
//   đi đâu cả, và cả file này gọn hơn phần cấu hình mà thư viện kia đòi.
//
// ĐỊNH DẠNG LƯU
//   Mỗi host một dòng "addr \t base64(iv|ciphertext) \t base64(iv|ciphertext)" cho
//   (password, token). Cùng kiểu bảng phẳng với Recents.kt để đọc bằng mắt được khi
//   gỡ lỗi — chỉ khác là hai cột sau đã mã hoá.
//
// LIÊN QUAN: NativeClient.nativeStart/nativeSubmitPassword/nativeTakeDeviceToken,
//            deskhub/auth/PasswordAuth.h, docs/04-protocol.md §7b
// =============================================================================
package com.deskhub.app.ui

import android.content.Context
import android.content.SharedPreferences
import android.security.keystore.KeyGenParameterSpec
import android.security.keystore.KeyProperties
import android.util.Base64
import java.security.KeyStore
import java.security.SecureRandom
import javax.crypto.Cipher
import javax.crypto.KeyGenerator
import javax.crypto.SecretKey
import javax.crypto.spec.GCMParameterSpec

/** Bí mật đã lưu cho một host. Trường rỗng = chưa có. */
data class HostCredential(
    val address: String,
    val password: String = "",
    val deviceToken: ByteArray = ByteArray(0),
) {
    val hasPassword: Boolean get() = password.isNotEmpty()
    val hasToken: Boolean get() = deviceToken.isNotEmpty()

    // data class sinh equals/hashCode dùng tham chiếu cho ByteArray — hai bản ghi
    // giống hệt nhau vẫn "khác nhau". Ghi đè để so theo NỘI DUNG, nếu không mọi phép
    // distinct/contains trên danh sách này đều cho kết quả sai.
    override fun equals(other: Any?): Boolean {
        if (this === other) return true
        if (other !is HostCredential) return false
        return address == other.address &&
            password == other.password &&
            deviceToken.contentEquals(other.deviceToken)
    }

    override fun hashCode(): Int {
        var h = address.hashCode()
        h = 31 * h + password.hashCode()
        h = 31 * h + deviceToken.contentHashCode()
        return h
    }
}

object Credentials {
    private const val PREFS = "deskhub"
    private const val KEY_ROWS = "credentials"
    private const val KEY_CLIENT_ID = "clientId"
    private const val KEYSTORE = "AndroidKeyStore"
    private const val KEY_ALIAS = "deskhub.credentials.v1"
    private const val ROW_SEP = "\n"
    private const val FIELD_SEP = "\t"
    private const val GCM_IV_BYTES = 12
    private const val GCM_TAG_BITS = 128

    private var prefs: SharedPreferences? = null
    private var cache: MutableList<HostCredential>? = null

    /** Gọi trước lần dùng đầu — cùng chỗ với Recents.init. */
    fun init(context: Context) {
        if (prefs == null) {
            prefs = context.applicationContext.getSharedPreferences(PREFS, Context.MODE_PRIVATE)
        }
    }

    /**
     * Danh tính ổn định của bản cài này, sinh một lần rồi giữ mãi.
     *
     * Khác 0 là bắt buộc: tầng C++ hiểu 0 là "caller quên truyền" và lùi về đồng hồ,
     * lúc đó tính năng nhớ thiết bị im lặng ngừng hoạt động.
     */
    val clientId: Int
        get() {
            val p = prefs ?: return 0
            val saved = p.getInt(KEY_CLIENT_ID, 0)
            if (saved != 0) return saved
            // SecureRandom chứ không phải Random: giá trị này đi vào proof xác thực
            // và là khoá tra thiết bị tin cậy phía host.
            var id = SecureRandom().nextInt()
            if (id == 0) id = 1
            p.edit().putInt(KEY_CLIENT_ID, id).apply()
            return id
        }

    /** Tên hiện ở danh sách "Trusted devices" phía host. */
    val deviceName: String
        get() = "${android.os.Build.MANUFACTURER} ${android.os.Build.MODEL}".trim()

    val all: List<HostCredential>
        get() = cache ?: load().toMutableList().also { cache = it }

    fun forAddress(address: String): HostCredential? =
        all.firstOrNull { it.address.equals(address.trim(), ignoreCase = true) }

    fun savePassword(
        address: String,
        password: String,
    ) {
        update(address) { it.copy(password = password) }
    }

    fun saveToken(
        address: String,
        token: ByteArray,
    ) {
        if (token.isEmpty()) return
        update(address) { it.copy(deviceToken = token) }
    }

    /**
     * Xoá RIÊNG mật khẩu đã lưu của một host, giữ lại deviceToken. Dùng khi host trả
     * lời "sai mật khẩu" cho bản đã lưu (họ đổi mật khẩu chẳng hạn) — giữ nó lại thì
     * mọi lần kết nối sau tự gửi proof sai và tiêu dần hạn mức 3 lần trước khi bị
     * khoá 5 phút. Không còn cả token thì rút hẳn bản ghi cho sạch.
     */
    fun forgetPassword(address: String) {
        val addr = address.trim()
        val list = all.toMutableList()
        val i = list.indexOfFirst { it.address.equals(addr, ignoreCase = true) }
        if (i < 0) return
        val cleared = list[i].copy(password = "")
        if (cleared.hasToken) list[i] = cleared else list.removeAt(i)
        cache = list
        save(list)
    }

    /**
     * Quên một host. Ứng với nút Forget ở màn "Saved passwords".
     *
     * Xoá CẢ mật khẩu lẫn token: giữ token lại thì "quên mật khẩu" vẫn vào được, và
     * người dùng bấm Forget chính vì không muốn máy này vào được nữa.
     */
    fun forget(address: String) {
        val list = all.toMutableList()
        if (list.removeAll { it.address.equals(address.trim(), ignoreCase = true) }) {
            cache = list
            save(list)
        }
    }

    fun forgetAll() {
        cache = mutableListOf()
        save(emptyList())
    }

    private fun update(
        address: String,
        edit: (HostCredential) -> HostCredential,
    ) {
        val addr = address.trim()
        if (addr.isEmpty()) return
        val list = all.toMutableList()
        val i = list.indexOfFirst { it.address.equals(addr, ignoreCase = true) }
        if (i >= 0) {
            list[i] = edit(list[i])
        } else {
            list.add(edit(HostCredential(addr)))
        }
        cache = list
        save(list)
    }

    // --- Mã hoá ---------------------------------------------------------------

    // Khoá AES-256 trong Keystore, sinh một lần ở lần dùng đầu. Không đặt
    // setUserAuthenticationRequired: khoá phải mở được ngay lúc khởi động để nạp
    // token, còn cửa sinh trắc học là một lớp RIÊNG ở tầng UI (xem ghi chú cuối file).
    private fun secretKey(): SecretKey {
        val ks = KeyStore.getInstance(KEYSTORE).apply { load(null) }
        (ks.getEntry(KEY_ALIAS, null) as? KeyStore.SecretKeyEntry)?.let { return it.secretKey }

        val gen = KeyGenerator.getInstance(KeyProperties.KEY_ALGORITHM_AES, KEYSTORE)
        gen.init(
            KeyGenParameterSpec
                .Builder(
                    KEY_ALIAS,
                    KeyProperties.PURPOSE_ENCRYPT or KeyProperties.PURPOSE_DECRYPT,
                ).setBlockModes(KeyProperties.BLOCK_MODE_GCM)
                .setEncryptionPaddings(KeyProperties.ENCRYPTION_PADDING_NONE)
                .setKeySize(256)
                .build(),
        )
        return gen.generateKey()
    }

    // IV đi kèm ciphertext (12 byte đầu). GCM đòi IV KHÔNG ĐƯỢC LẶP với cùng một
    // khoá — dùng lại một IV cố định sẽ phá vỡ hoàn toàn tính bí mật của GCM. Ở đây
    // Cipher tự sinh IV mới cho mỗi lần init encrypt, nên chỉ việc lưu kèm nó.
    private fun encrypt(plain: ByteArray): String {
        if (plain.isEmpty()) return ""
        return try {
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(Cipher.ENCRYPT_MODE, secretKey())
            val out = c.iv + c.doFinal(plain)
            Base64.encodeToString(out, Base64.NO_WRAP)
        } catch (e: Exception) {
            // Không lưu được thì thà mất bí mật đã lưu còn hơn ghi bản RÕ xuống đĩa.
            android.util.Log.w("Deskhub", "credential encrypt failed: ${e.message}")
            ""
        }
    }

    private fun decrypt(encoded: String): ByteArray {
        if (encoded.isEmpty()) return ByteArray(0)
        return try {
            val raw = Base64.decode(encoded, Base64.NO_WRAP)
            if (raw.size <= GCM_IV_BYTES) return ByteArray(0)
            val c = Cipher.getInstance("AES/GCM/NoPadding")
            c.init(
                Cipher.DECRYPT_MODE,
                secretKey(),
                GCMParameterSpec(GCM_TAG_BITS, raw, 0, GCM_IV_BYTES),
            )
            c.doFinal(raw, GCM_IV_BYTES, raw.size - GCM_IV_BYTES)
        } catch (e: Exception) {
            // Khoá Keystore biến mất (gỡ vân tay, khôi phục máy khác, đổi khoá màn
            // hình trên vài máy) → giải mã hỏng. Coi như chưa lưu gì: người dùng gõ
            // lại mật khẩu một lần là xong, còn hơn app không mở được.
            android.util.Log.w("Deskhub", "credential decrypt failed: ${e.message}")
            ByteArray(0)
        }
    }

    private fun load(): List<HostCredential> {
        val raw = prefs?.getString(KEY_ROWS, null).orEmpty()
        if (raw.isEmpty()) return emptyList()
        return raw.split(ROW_SEP).mapNotNull { row ->
            val f = row.split(FIELD_SEP)
            if (f.size < 3) return@mapNotNull null
            HostCredential(
                address = f[0],
                password = String(decrypt(f[1]), Charsets.UTF_8),
                deviceToken = decrypt(f[2]),
            )
        }
    }

    private fun save(list: List<HostCredential>) {
        val raw =
            list.joinToString(ROW_SEP) {
                listOf(
                    it.address.replace(FIELD_SEP, " ").replace(ROW_SEP, " "),
                    encrypt(it.password.toByteArray(Charsets.UTF_8)),
                    encrypt(it.deviceToken),
                ).joinToString(FIELD_SEP)
            }
        prefs?.edit()?.putString(KEY_ROWS, raw)?.apply()
    }
}
