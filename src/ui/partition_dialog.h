#ifndef PARTITION_DIALOG_H
#define PARTITION_DIALOG_H

#include <wx/wx.h>
#include "neko_installer.h"
#include "password_field.h"

class PartitionDialog : public wxDialog {
public:
    PartitionDialog(wxWindow *parent, AppData *app, PartitionConfig *edit_conf);
    ~PartitionDialog();

private:
    AppData *app_;
    PartitionConfig *edit_conf_;

    wxChoice *partChoice_;
    wxChoice *fsChoice_;
    wxTextCtrl *mountEntry_;
    wxCheckBox *formatCheck_;
    wxCheckBox *encryptCheck_;
    PasswordField *passEntry_;
    PasswordField *passConfirmEntry_;

    void OnFsChanged(wxCommandEvent &ev);
    void OnEncryptToggled(wxCommandEvent &ev);
    void OnOk(wxCommandEvent &ev);

    void PopulatePartitions();
    void ApplyToEdit(const wxString &dev, const wxString &fs, const wxString &mp,
                     bool fmt, bool encrypt, const wxString &pass);

    wxDECLARE_EVENT_TABLE();
};

#endif
