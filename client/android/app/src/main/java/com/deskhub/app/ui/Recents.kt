// =============================================================================
// Recents.kt — danh sách máy đã kết nối, lưu giữa các lần chạy.
//              Đối ứng client/ios/app/swift/Recents.swift (đọc ghi chú dài ở đó);
//              cùng định dạng lưu, cùng luật đoán loại đường.
//
// Bản cũ chỉ nhớ ĐÚNG MỘT địa chỉ (khoá "addr" trong SharedPreferences). Ai dùng hai
// máy thì mỗi lần đổi là gõ lại một chuỗi IP trên bàn phím ảo — thao tác khó chịu
// nhất mà cái app này bắt người dùng làm.
// =============================================================================
package com.deskhub.app.ui

import android.content.Context
import android.content.SharedPreferences

data class RecentMachine(
    val address: String,
    val name: String,
    val link: String,
) {
    // Tên rỗng (kết nối bằng tay, host chưa xưng tên) thì địa chỉ chính là tên.
    val displayName: String get() = name.ifEmpty { address }
}

object Recents {
    private const val KEY = "recentMachines"
    private const val MAX = 12
    private const val ROW_SEP = "\n"
    private const val FIELD_SEP = "\t"

    private var prefs: SharedPreferences? = null
    private var cache: List<RecentMachine>? = null

    /** Gọi trước lần dùng đầu — MainActivity làm việc đó cùng chỗ với AppState.init. */
    fun init(context: Context) {
        if (prefs == null) {
            prefs = context.applicationContext.getSharedPreferences("deskhub", Context.MODE_PRIVATE)
        }
    }

    val all: List<RecentMachine>
        get() = cache ?: load().also { cache = it }

    // Ghi nhận một lần kết nối. Địa chỉ trùng thì cập nhật tại chỗ và đẩy lên đầu —
    // danh sách sắp theo lần dùng gần nhất, đúng thứ tự người ta đi tìm.
    fun remember(
        address: String,
        name: String = "",
    ) {
        val addr = address.trim()
        if (addr.isEmpty()) return
        // Tên mới rỗng thì GIỮ tên đã lưu: hầu hết call site chỉ biết địa chỉ, và
        // ghi đè bằng chuỗi rỗng sẽ xoá mất tên hiển thị mà một lần trước đã có.
        val newName = clean(name).ifEmpty { all.firstOrNull { it.address == addr }?.name.orEmpty() }
        val list =
            buildList {
                add(RecentMachine(addr, newName, guessLink(addr)))
                addAll(all.filter { it.address != addr })
            }.take(MAX)
        cache = list
        save(list)
    }

    fun forgetAll() {
        cache = emptyList()
        save(emptyList())
    }

    // Đoán loại đường từ dải IP. Chỉ để hiện nhãn nên đoán sai không hỏng gì —
    // 100.64.0.0/10 là dải CGNAT mà Tailscale dùng, phần còn lại thì không biết.
    fun guessLink(address: String): String {
        val host = address.substringBefore(':')
        val octets = host.split('.')
        if (octets.size != 4) return ""
        val o1 = octets[0].toIntOrNull() ?: return ""
        val o2 = octets[1].toIntOrNull() ?: return ""
        if (o1 == 100 && o2 in 64..127) return "Tailscale"
        if (o1 == 10 || (o1 == 192 && o2 == 168) || (o1 == 172 && o2 in 16..31)) return "LAN"
        return ""
    }

    // Bỏ ký tự phân tách khỏi dữ liệu người dùng: một tên máy có tab trong đó sẽ làm
    // lệch mọi trường phía sau khi đọc lại.
    private fun clean(text: String) = text.replace(FIELD_SEP, " ").replace(ROW_SEP, " ")

    private fun load(): List<RecentMachine> {
        val raw = prefs?.getString(KEY, null).orEmpty()
        if (raw.isEmpty()) return emptyList()
        return raw.split(ROW_SEP).mapNotNull { row ->
            val fields = row.split(FIELD_SEP)
            if (fields.size < 3) return@mapNotNull null
            RecentMachine(fields[0], fields[1], fields[2])
        }
    }

    private fun save(list: List<RecentMachine>) {
        val raw =
            list.joinToString(ROW_SEP) {
                listOf(it.address, it.name, it.link).joinToString(FIELD_SEP)
            }
        prefs?.edit()?.putString(KEY, raw)?.apply()
    }
}
