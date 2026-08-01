extension DeskhubClient {
    static func mapKey(_ macKeyCode: UInt16) -> (vk: Int32, scan: Int32)? {
        var vk: Int32 = 0
        var scan: Int32 = 0
        guard dh_native_key_to_vk(Int32(macKeyCode), &vk, &scan) else { return nil }
        return (vk, scan)
    }
}
