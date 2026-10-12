#ifndef WX_FRAME_H
#define WX_FRAME_H

#include <wx/wx.h>
#include <wx/notebook.h>
#include <wx/listctrl.h>
#include <wx/gauge.h>
#include <wx/timer.h>
#include <string>
#include <vector>
#include "neko_installer.h"
#include "password_field.h"

struct DesktopOpt {
    wxCheckBox *chk;
    wxStaticText *desc;
    std::string id;      // "xfce", "kde", ...
    std::string descKey; // "d_xfce_desc", ...
};

class MainFrame : public wxFrame {
public:
    MainFrame(AppData *app);
    ~MainFrame();

private:
    AppData *app_;

    wxNotebook *notebook_;
    wxButton *backBtn_;
    wxButton *nextBtn_;
    wxButton *installBtn_;

    // Welcome
    wxStaticText *welcomeTitle_;
    wxStaticText *welcomeBody_;
    wxStaticText *langLabel_;
    wxChoice *langChoice_;

    // Install type
    wxStaticText *installTypeTitle_;
    wxRadioButton *cleanRadio_;
    wxRadioButton *alongRadio_;
    wxStaticText *installTypeDesc_;

    // Partitions
    wxStaticText *diskTitle_;
    wxChoice *diskChoice_;
    wxButton *gpartedBtn_;
    wxStaticText *diskInfo_;
    wxStaticBoxSizer *existingBox_;
    wxListCtrl *existingList_;
    wxCheckBox *manualCheck_;
    wxStaticBoxSizer *installBox_;
    wxStaticText *partMountLabel_;
    wxButton *addBtn_;
    wxButton *editBtn_;
    wxButton *delBtn_;
    wxButton *resetBtn_;
    wxListCtrl *mountList_;

    // Bootloader
    wxStaticText *bootStatus_;
    wxStaticText *grubLabel_;
    wxChoice *grubChoice_;

    // System
    wxStaticText *hostnameLabel_;
    wxTextCtrl *hostnameEntry_;
    wxStaticText *countryLabel_;
    wxChoice *countryChoice_;
    wxStaticText *localeLabel_;
    wxChoice *localeChoice_;
    wxStaticText *regionLabel_;
    wxChoice *tzAreaChoice_;
    wxStaticText *cityLabel_;
    wxChoice *tzCityChoice_;
    wxStaticText *kbdLayoutLabel_;
    wxChoice *kbdLayoutChoice_;
    wxStaticText *kbdVariantLabel_;
    wxChoice *kbdVariantChoice_;

    // Desktop
    wxStaticText *desktopTitle_;
    wxStaticText *desktopDesc_;
    wxCheckBox *desktopLocal_;
    wxStaticText *desktopLocalDesc_;
    wxPanel *desktopSelPanel_;
    std::vector<DesktopOpt> desktopOpts_;

    // Users
    wxStaticText *fullnameLabel_;
    wxTextCtrl *fullnameEntry_;
    wxStaticText *usernameLabel_;
    wxTextCtrl *usernameEntry_;
    wxStaticText *userPassLabel_;
    PasswordField *userPassEntry_;
    wxStaticText *userConfirmLabel_;
    PasswordField *userPassConfirmEntry_;
    wxStaticText *rootPassLabel_;
    PasswordField *rootPassEntry_;
    wxCheckBox *autologinCheck_;

    // Security
    wxStaticText *privTitle_;
    wxStaticText *privDesc_;
    wxRadioButton *sudoRadio_;
    wxRadioButton *doasRadio_;
    wxStaticText *privSudoDesc_;
    wxStaticText *privDoasDesc_;

    // Install
    wxTextCtrl *console_;
    wxGauge *gauge_;

    wxTimer *drainTimer_;
    wxTimer *pulseTimer_;
    int pulseValue_;

    std::vector<std::string> diskNames_;

    // Construction
    wxPanel *BuildWelcome();
    wxPanel *BuildInstallType();
    wxPanel *BuildPartitions();
    wxPanel *BuildBootloader();
    wxPanel *BuildSystem();
    wxPanel *BuildDesktop();
    wxPanel *BuildUsers();
    wxPanel *BuildSecurity();
    wxPanel *BuildInstall();
    void BuildNav();
    void ScanDisks();
    void RefreshPartitionList();
    void RefreshExistingPartitions();
    void PopulateCountries();
    void PopulateLocales();
    void PopulateKbdLayouts();
    void PopulateKbdVariants();
    void PopulateTzCities();
    void HandleDiskSelection();
    void HandleCountrySelection();
    void UpdateUiLanguage();
    void UpdateNavButtons();
    const char *ValidateTab(int page);
    void CollectInstallValues();
    void OnInstallFinished();
    void ShowPartitionDialog(PartitionConfig *edit);
    void SetSelectedDesktop(const char *name);
    void UncheckDesktopGroup(wxCheckBox *keep);

    // Events
    void OnClose(wxCloseEvent &ev);
    void OnNext(wxCommandEvent &ev);
    void OnBack(wxCommandEvent &ev);
    void OnPageChanged(wxBookCtrlEvent &ev);
    void OnLangChanged(wxCommandEvent &ev);
    void OnDiskChanged(wxCommandEvent &ev);
    void OnCountryChanged(wxCommandEvent &ev);
    void OnTzAreaChanged(wxCommandEvent &ev);
    void OnTzCityChanged(wxCommandEvent &ev);
    void OnKbdLayoutChanged(wxCommandEvent &ev);
    void OnKbdVariantChanged(wxCommandEvent &ev);
    void OnInstallTypeChanged(wxCommandEvent &ev);
    void OnManualCheck(wxCommandEvent &ev);
    void OnAddPartition(wxCommandEvent &ev);
    void OnEditPartition(wxCommandEvent &ev);
    void OnDeletePartition(wxCommandEvent &ev);
    void OnResetPartitions(wxCommandEvent &ev);
    void OnGParted(wxCommandEvent &ev);
    void OnStartInstall(wxCommandEvent &ev);
    void OnReboot(wxCommandEvent &ev);
    void OnDesktopLocal(wxCommandEvent &ev);
    void OnDesktopSelected(wxCommandEvent &ev);
    void OnUsernameChar(wxKeyEvent &ev);
    void OnHostnameChar(wxKeyEvent &ev);
    void OnDrainTimer(wxTimerEvent &ev);
    void OnPulseTimer(wxTimerEvent &ev);

    wxDECLARE_EVENT_TABLE();
};

#endif
