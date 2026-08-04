extension DeskhubClient {
    static func mapKey(_ nativeKeyCode: Int32) -> (vk: Int32, scan: Int32)? {
        var vk: Int32 = 0
        var scan: Int32 = 0
        guard dh_native_key_to_vk(nativeKeyCode, &vk, &scan) else { return nil }
        return (vk, scan)
    }
}
