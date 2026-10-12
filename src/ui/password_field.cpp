#include "password_field.h"

// U+1F441 "eye" (👁) encoded as UTF-8
static const char *EYE_GLYPH = "\xF0\x9F\x91\x81";

wxBEGIN_EVENT_TABLE(PasswordField, wxPanel)
wxEND_EVENT_TABLE()

PasswordField::PasswordField(wxWindow *parent, const wxString &hint)
    : wxPanel(parent, wxID_ANY), shown_(false), hint_(hint) {

    sizer_ = new wxBoxSizer(wxHORIZONTAL);

    entry_ = new wxTextCtrl(this, wxID_ANY, wxEmptyString, wxDefaultPosition,
                            wxDefaultSize, wxTE_PASSWORD);
    if (!hint_.empty()) entry_->SetHint(hint_);

    eye_ = new wxButton(this, wxID_ANY, wxString::FromUTF8(EYE_GLYPH),
                        wxDefaultPosition, wxSize(36, -1));
    eye_->SetToolTip(wxT("Show/Hide password"));
    eye_->Bind(wxEVT_BUTTON, &PasswordField::OnToggle, this);

    sizer_->Add(entry_, wxSizerFlags(1).CentreVertical());
    sizer_->Add(eye_, wxSizerFlags().CentreVertical().Border(wxLEFT, 4));
    SetSizer(sizer_);
}

wxString PasswordField::GetValue() const { return entry_->GetValue(); }

void PasswordField::SetValue(const wxString &value) { entry_->SetValue(value); }

bool PasswordField::Enable(bool enable) {
    bool r = wxPanel::Enable(enable);
    if (entry_) entry_->Enable(enable);
    if (eye_) eye_->Enable(enable);
    return r;
}

void PasswordField::OnToggle(wxCommandEvent &) {
    shown_ = !shown_;
    Rebuild();
}

void PasswordField::Rebuild() {
    wxString value = entry_->GetValue();
    long insertion = entry_->GetInsertionPoint();
    long from = 0, to = 0;
    entry_->GetSelection(&from, &to);

    long style = shown_ ? 0 : wxTE_PASSWORD;
    wxTextCtrl *newEntry = new wxTextCtrl(this, wxID_ANY, value, wxDefaultPosition,
                                          wxDefaultSize, style);
    if (!hint_.empty()) newEntry->SetHint(hint_);
    newEntry->Enable(IsEnabled());
    newEntry->SetInsertionPoint(insertion);
    newEntry->SetSelection(from, to);

    sizer_->Detach(entry_);
    entry_->Destroy();
    entry_ = newEntry;
    sizer_->Insert(0, entry_, wxSizerFlags(1).CentreVertical());

    Layout();
    entry_->SetFocus();
}
