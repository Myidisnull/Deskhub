enum ClientRoute: Sendable {
    case connect
    case connected
    case sourcePicker([Source])
    case stream
    case sharing
}
