#ifndef PASSWORD_FIELD_H
#define PASSWORD_FIELD_H

#include <wx/wx.h>

/*
 * A password text entry with a trailing "eye" button that toggles between
 * masked and plain text. wxWidgets cannot toggle wxTE_PASSWORD at runtime,
 * so the inner wxTextCtrl is recreated (preserving value/selection/focus).
 */
class PasswordField : public wxPanel {
public:
    PasswordField(wxWindow *parent, const wxString &hint = wxEmptyString);

    wxString GetValue() const;
    void SetValue(const wxString &value);
    wxTextCtrl *Entry() const { return entry_; }

    bool Enable(bool enable = true) override;

private:
    wxBoxSizer *sizer_;
    wxTextCtrl *entry_;
    wxButton *eye_;
    bool shown_;
    wxString hint_;

    void Rebuild();
    void OnToggle(wxCommandEvent &ev);

    wxDECLARE_EVENT_TABLE();
};

#endif
