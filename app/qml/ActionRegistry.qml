pragma Singleton

import QtQuick

QtObject {
    id: registry

    // A surface contributes once here. Both Shortcut instances and the
    // command palette consume the same flattened action records.
    property var registrations: ({})
    property var actions: []

    function replace(source, contributedActions) {
        const next = Object.assign({}, registrations)
        next[source] = contributedActions
        registrations = next
        rebuild()
    }

    function remove(source) {
        const next = Object.assign({}, registrations)
        delete next[source]
        registrations = next
        rebuild()
    }

    function rebuild() {
        let flattened = []
        for (const source of Object.keys(registrations))
            flattened = flattened.concat(registrations[source])
        actions = flattened
    }

    function trigger(id) {
        for (const action of actions) {
            if (action.id === id && action.enabled !== false) {
                action.invoke()
                return true
            }
        }
        return false
    }

    function triggerBinding(binding) {
        for (const action of actions) {
            if (action.binding === binding && action.enabled !== false)
                return trigger(action.id)
        }
        return false
    }
}
