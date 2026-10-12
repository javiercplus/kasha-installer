#include "partition_dialog.h"
#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/checkbox.h>
#include <wx/button.h>
#include <wx/statbox.h>
#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <vector>
#include <algorithm>

namespace {
char *dupstr(const wxString &s) { return g_strdup(s.utf8_str()); }
}

enum {
    ID_FS = wxID_HIGHEST + 1,
    ID_ENCRYPT,
    ID_OK
};

wxBEGIN_EVENT_TABLE(PartitionDialog, wxDialog)
    EVT_CHOICE(ID_FS, PartitionDialog::OnFsChanged)
    EVT_CHECKBOX(ID_ENCRYPT, PartitionDialog::OnEncryptToggled)
    EVT_BUTTON(ID_OK, PartitionDialog::OnOk)
wxEND_EVENT_TABLE()

static wxString loc(AppData *app, const char *key) {
    return wxString::FromUTF8(get_loc(key, app->current_lang));
}

PartitionDialog::PartitionDialog(wxWindow *parent, AppData *app, PartitionConfig *edit_conf)
    : wxDialog(parent, wxID_ANY,
               edit_conf ? loc(app, "dialog_edit") : loc(app, "dialog_add"),
               wxDefaultPosition, wxDefaultSize,
               wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER),
      app_(app), edit_conf_(edit_conf) {

    wxBoxSizer *main = new wxBoxSizer(wxVERTICAL);

    // Device
    main->Add(new wxStaticText(this, wxID_ANY, loc(app, "select_partition")),
              wxSizerFlags().Border(wxTOP | wxLEFT | wxRIGHT, 10));
    partChoice_ = new wxChoice(this, wxID_ANY);
    PopulatePartitions();
    main->Add(partChoice_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 10));

    // Filesystem
    main->Add(new wxStaticText(this, wxID_ANY, loc(app, "filesystem")),
              wxSizerFlags().Border(wxTOP | wxLEFT | wxRIGHT, 10));
    fsChoice_ = new wxChoice(this, ID_FS);
    const char *fss[] = { "ext4", "btrfs", "xfs", "f2fs", "vfat", "swap" };
    for (int i = 0; i < 6; i++) fsChoice_->Append(wxString::FromUTF8(fss[i]));
    fsChoice_->SetSelection(0);
    main->Add(fsChoice_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 10));

    // Mount point
    main->Add(new wxStaticText(this, wxID_ANY, loc(app, "mount_point")),
              wxSizerFlags().Border(wxTOP | wxLEFT | wxRIGHT, 10));
    mountEntry_ = new wxTextCtrl(this, wxID_ANY, wxT("/"));
    main->Add(mountEntry_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT, 10));

    // Format
    formatCheck_ = new wxCheckBox(this, wxID_ANY, loc(app, "format_partition"));
    formatCheck_->SetValue(true);
    main->Add(formatCheck_, wxSizerFlags().Border(wxLEFT | wxRIGHT | wxTOP, 10));

    // Encryption
    wxStaticBoxSizer *encBox = new wxStaticBoxSizer(wxVERTICAL, this, loc(app, "encryption"));
    encryptCheck_ = new wxCheckBox(encBox->GetStaticBox(), ID_ENCRYPT, loc(app, "encrypt_luks"));
    encBox->Add(encryptCheck_, wxSizerFlags().Border(wxALL, 5));

    passEntry_ = new wxTextCtrl(encBox->GetStaticBox(), wxID_ANY, wxT(""),
                                wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    passConfirmEntry_ = new wxTextCtrl(encBox->GetStaticBox(), wxID_ANY, wxT(""),
                                       wxDefaultPosition, wxDefaultSize, wxTE_PASSWORD);
    encBox->Add(passEntry_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 5));
    encBox->Add(passConfirmEntry_, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxBOTTOM, 5));
    passEntry_->Enable(false);
    passConfirmEntry_->Enable(false);

    main->Add(encBox, wxSizerFlags().Expand().Border(wxALL, 10));

    // Buttons
    wxBoxSizer *btns = new wxBoxSizer(wxHORIZONTAL);
    wxButton *cancelBtn = new wxButton(this, wxID_CANCEL, loc(app, "cancel"));
    wxButton *okBtn = new wxButton(this, ID_OK, edit_conf ? loc(app, "dialog_update") : loc(app, "save"));
    okBtn->SetDefault();
    btns->AddStretchSpacer();
    btns->Add(cancelBtn, wxSizerFlags().Border(wxRIGHT, 5));
    btns->Add(okBtn);
    main->Add(btns, wxSizerFlags().Expand().Border(wxALL, 10));

    // Pre-fill if editing
    if (edit_conf) {
        const char *short_dev = edit_conf->device;
        if (strncmp(short_dev, "/dev/", 5) == 0) short_dev += 5;
        int found = partChoice_->FindString(wxString::FromUTF8(short_dev));
        if (found == wxNOT_FOUND) {
            partChoice_->Append(wxString::FromUTF8(short_dev));
            found = partChoice_->GetCount() - 1;
        }
        partChoice_->SetSelection(found);

        int fsIdx = 0;
        if (strcmp(edit_conf->fstype, "btrfs") == 0) fsIdx = 1;
        else if (strcmp(edit_conf->fstype, "xfs") == 0) fsIdx = 2;
        else if (strcmp(edit_conf->fstype, "f2fs") == 0) fsIdx = 3;
        else if (strcmp(edit_conf->fstype, "vfat") == 0) fsIdx = 4;
        else if (strcmp(edit_conf->fstype, "swap") == 0) fsIdx = 5;
        fsChoice_->SetSelection(fsIdx);

        mountEntry_->SetValue(strcmp(edit_conf->fstype, "swap") == 0
                              ? wxT("[SWAP]") : wxString::FromUTF8(edit_conf->mountpoint));
        formatCheck_->SetValue(edit_conf->format);
        encryptCheck_->SetValue(edit_conf->encrypt);
        if (edit_conf->luks_pass) {
            passEntry_->SetValue(wxString::FromUTF8(edit_conf->luks_pass));
            passConfirmEntry_->SetValue(wxString::FromUTF8(edit_conf->luks_pass));
        }
        passEntry_->Enable(edit_conf->encrypt);
        passConfirmEntry_->Enable(edit_conf->encrypt);
    } else {
        partChoice_->SetSelection(0);
    }

    SetSizerAndFit(main);
    CentreOnParent();
}

PartitionDialog::~PartitionDialog() {}

void PartitionDialog::PopulatePartitions() {
    partChoice_->Clear();
    if (!app_->selected_disk) return;

    wxString sysPath = wxString::Format(wxT("/sys/block/%s"),
                                        wxString::FromUTF8(app_->selected_disk));
    DIR *d = opendir(sysPath.utf8_str());
    if (!d) return;

    std::vector<wxString> names;
    struct dirent *ent;
    while ((ent = readdir(d)) != NULL) {
        wxString dname = wxString::FromUTF8(ent->d_name);
        wxString disk = wxString::FromUTF8(app_->selected_disk);
        if (dname.StartsWith(disk) && dname != disk) {
            wxString sizePath = sysPath + wxT("/") + dname + wxT("/size");
            if (access(sizePath.utf8_str(), F_OK) == 0) {
                wxString devPath = wxT("/dev/") + dname;
                char fstype[64];
                get_partition_fstype(devPath.utf8_str(), fstype, sizeof(fstype));
                if (strcmp(fstype, "iso9660") == 0 || strcmp(fstype, "crypto_LUKS") == 0 ||
                    strcmp(fstype, "LVM2_member") == 0) {
                    continue;
                }
                names.push_back(dname);
            }
        }
    }
    closedir(d);
    std::sort(names.begin(), names.end());
    for (const auto &n : names) partChoice_->Append(n);
}

void PartitionDialog::OnFsChanged(wxCommandEvent &) {
    wxString fs = fsChoice_->GetStringSelection();
    if (fs == wxT("swap")) {
        mountEntry_->Enable(false);
        mountEntry_->SetValue(wxT("[SWAP]"));
    } else {
        mountEntry_->Enable(true);
        wxString cur = mountEntry_->GetValue();
        if (cur.empty() || cur == wxT("[SWAP]"))
            mountEntry_->SetValue(wxT("/"));
    }
}

void PartitionDialog::OnEncryptToggled(wxCommandEvent &) {
    bool on = encryptCheck_->GetValue();
    passEntry_->Enable(on);
    passConfirmEntry_->Enable(on);
}

void PartitionDialog::ApplyToEdit(const wxString &dev, const wxString &fs, const wxString &mp,
                                  bool fmt, bool encrypt, const wxString &pass) {
    g_free(edit_conf_->device);
    g_free(edit_conf_->fstype);
    g_free(edit_conf_->mountpoint);
    if (edit_conf_->luks_pass) g_free(edit_conf_->luks_pass);
    if (edit_conf_->original_device) g_free(edit_conf_->original_device);
    if (edit_conf_->luks_uuid) g_free(edit_conf_->luks_uuid);

    wxString full = wxT("/dev/") + dev;
    edit_conf_->device = dupstr(full);
    edit_conf_->original_device = dupstr(full);
    edit_conf_->fstype = dupstr(fs);
    edit_conf_->mountpoint = dupstr(mp);
    edit_conf_->format = fmt;
    edit_conf_->encrypt = encrypt;
    edit_conf_->luks_pass = (encrypt && !pass.empty()) ? dupstr(pass) : NULL;
    edit_conf_->luks_uuid = NULL;
}

void PartitionDialog::OnOk(wxCommandEvent &) {
    wxString dev = partChoice_->GetStringSelection();
    wxString fs = fsChoice_->GetStringSelection();
    wxString mp = mountEntry_->GetValue();
    bool fmt = formatCheck_->GetValue();
    bool encrypt = encryptCheck_->GetValue();
    wxString pass = passEntry_->GetValue();
    wxString passConf = passConfirmEntry_->GetValue();

    if (encrypt && (pass.empty() || pass != passConf)) {
        wxMessageDialog dlg(this,
                            wxT("Encryption passwords do not match or are empty!"),
                            wxT("Error"), wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return;
    }

    if (!dev.empty() && !mp.empty()) {
        if (app_->manual_partitioning) {
            app_->install_mode = INSTALL_MODE_MANUAL;
        }
        wxString storedMp = (fs == wxT("swap")) ? wxT("[SWAP]") : mp;

        if (edit_conf_) {
            ApplyToEdit(dev, fs, storedMp, fmt, encrypt, pass);
        } else {
            wxString full = wxT("/dev/") + dev;
            add_partition_config(app_, full.utf8_str(), fs.utf8_str(), storedMp.utf8_str(),
                                 fmt, encrypt, pass.utf8_str());
        }
        EndModal(wxID_OK);
    }
}
