enum ClientRoute: Sendable {
    case connect
    case sourcePicker([Source])
    case stream
    case sharing
}
