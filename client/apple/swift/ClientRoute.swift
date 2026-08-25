enum ClientRoute: Sendable {
    case connect
    case connected
    case sourcePicker([Source])
    case stream
    case terminal
    case sharing
}
