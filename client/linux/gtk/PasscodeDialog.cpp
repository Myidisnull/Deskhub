#include "gtk/PasscodeDialog.h"

#include "deskhub/protocol/Wire.h"
#include "deskhub/ui/Strings.h"

namespace {

GtkWidget* DeviceLabel(const std::string& addr) {
    GtkWidget* label = gtk_label_new(nullptr);
    char* markup = g_markup_printf_escaped("<b><big>%s</big></b>", addr.c_str());
    gtk_label_set_markup(GTK_LABEL(label), markup);
    g_free(markup);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    return label;
}

GtkWidget* HintLabel(const char* text) {
    GtkWidget* label = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(label), 0.f);
    gtk_widget_set_sensitive(label, FALSE);
    return label;
}

}

bool ShowPasscodeDialog(GtkWindow* parent, std::string& addr, std::string& passcode) {
    GtkWidget* dlg = gtk_dialog_new_with_buttons(deskhub::ui::kConnectPromptTitle, parent,
        GTK_DIALOG_MODAL, "_Cancel", GTK_RESPONSE_CANCEL, "_Connect", GTK_RESPONSE_ACCEPT,
        nullptr);
    gtk_dialog_set_default_response(GTK_DIALOG(dlg), GTK_RESPONSE_ACCEPT);

    GtkWidget* box = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_set_spacing(GTK_BOX(box), 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 16);

    const std::string host = deskhub::ui::AddressHost(addr);
    gtk_box_pack_start(GTK_BOX(box), DeviceLabel(host), FALSE, FALSE, 0);

    GtkWidget* portRow = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* portPrompt = gtk_label_new(deskhub::ui::kUdpPortLabel);
    gtk_label_set_xalign(GTK_LABEL(portPrompt), 0.f);
    gtk_box_pack_start(GTK_BOX(portRow), portPrompt, FALSE, FALSE, 0);

    const uint16_t knownPort = deskhub::ui::AddressPort(addr);
    GtkWidget* portEntry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(portEntry),
        std::to_string(knownPort != 0 ? knownPort : deskhub::kDeskhubPort).c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(portEntry), 6);
    gtk_entry_set_max_length(GTK_ENTRY(portEntry), 5);
    gtk_entry_set_input_purpose(GTK_ENTRY(portEntry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_activates_default(GTK_ENTRY(portEntry), TRUE);
    gtk_box_pack_start(GTK_BOX(portRow), portEntry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), portRow, FALSE, FALSE, 0);

    GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    GtkWidget* prompt = gtk_label_new(deskhub::ui::kClientPasscodePrompt);
    gtk_label_set_xalign(GTK_LABEL(prompt), 0.f);
    gtk_box_pack_start(GTK_BOX(row), prompt, FALSE, FALSE, 0);

    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(entry), passcode.c_str());
    gtk_entry_set_width_chars(GTK_ENTRY(entry), gint(deskhub::kPasscodeDigits));
    gtk_entry_set_max_length(GTK_ENTRY(entry), gint(deskhub::kPasscodeDigits));
    gtk_entry_set_input_purpose(GTK_ENTRY(entry), GTK_INPUT_PURPOSE_DIGITS);
    gtk_entry_set_activates_default(GTK_ENTRY(entry), TRUE);
    gtk_box_pack_start(GTK_BOX(row), entry, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    gtk_box_pack_start(GTK_BOX(box), HintLabel(deskhub::ui::kClientPasscodeHint), FALSE, FALSE, 0);

    gtk_widget_show_all(dlg);
    gtk_widget_grab_focus(entry);

    const bool accepted = gtk_dialog_run(GTK_DIALOG(dlg)) == GTK_RESPONSE_ACCEPT;
    if (accepted) {
        passcode = gtk_entry_get_text(GTK_ENTRY(entry));
        addr = deskhub::ui::AddressWithPort(host,
            deskhub::ui::PortOrDefault(gtk_entry_get_text(GTK_ENTRY(portEntry))));
    }
    gtk_widget_destroy(dlg);
    return accepted;
}
