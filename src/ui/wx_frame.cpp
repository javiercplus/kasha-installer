#include "wx_frame.h"
#include "partition_dialog.h"
#include "country_data.h"
#include "lang.h"
#include "logo.h"

#include <wx/sizer.h>
#include <wx/stattext.h>
#include <wx/statbox.h>
#include <wx/checkbox.h>
#include <wx/radiobut.h>
#include <wx/button.h>
#include <wx/choice.h>
#include <wx/textctrl.h>
#include <wx/panel.h>
#include <wx/statbmp.h>
#include <wx/image.h>
#include <wx/mstream.h>
#include <wx/msgdlg.h>
#include <wx/statline.h>

#include <dirent.h>
#include <unistd.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <algorithm>

#ifdef HAS_DESKTOP_TAB
#define TAB_USERS 6
#define TAB_SECURITY 7
#define TAB_INSTALL 8
#define TAB_COUNT 9
#define LAST_EDITABLE 6
#else
#define TAB_USERS 5
#define TAB_SECURITY 6
#define TAB_INSTALL 7
#define TAB_COUNT 8
#define LAST_EDITABLE 5
#endif

enum {
    ID_LANG = wxID_HIGHEST + 1,
    ID_CLEAN,
    ID_ALONGSIDE,
    ID_DISK,
    ID_GPARTED,
    ID_MANUAL,
    ID_ADD,
    ID_EDIT,
    ID_DEL,
    ID_RESET,
    ID_GRUB,
    ID_COUNTRY,
    ID_TZ_AREA,
    ID_TZ_CITY,
    ID_KBD_LAYOUT,
    ID_KBD_VARIANT,
    ID_HOSTNAME,
    ID_USERNAME,
    ID_SUDO,
    ID_DOAS,
    ID_DESKTOP_LOCAL,
    ID_DESKTOP_FIRST,
    ID_BACK,
    ID_NEXT,
    ID_INSTALL,
    ID_DRAIN_TIMER,
    ID_PULSE_TIMER
};

wxBEGIN_EVENT_TABLE(MainFrame, wxFrame)
    EVT_CLOSE(MainFrame::OnClose)
    EVT_BUTTON(ID_NEXT, MainFrame::OnNext)
    EVT_BUTTON(ID_BACK, MainFrame::OnBack)
    EVT_BUTTON(ID_INSTALL, MainFrame::OnStartInstall)
    EVT_NOTEBOOK_PAGE_CHANGED(wxID_ANY, MainFrame::OnPageChanged)

    EVT_CHOICE(ID_LANG, MainFrame::OnLangChanged)
    EVT_CHOICE(ID_DISK, MainFrame::OnDiskChanged)
    EVT_CHOICE(ID_COUNTRY, MainFrame::OnCountryChanged)
    EVT_CHOICE(ID_TZ_AREA, MainFrame::OnTzAreaChanged)
    EVT_CHOICE(ID_TZ_CITY, MainFrame::OnTzCityChanged)
    EVT_CHOICE(ID_KBD_LAYOUT, MainFrame::OnKbdLayoutChanged)
    EVT_CHOICE(ID_KBD_VARIANT, MainFrame::OnKbdVariantChanged)

    EVT_RADIOBUTTON(ID_CLEAN, MainFrame::OnInstallTypeChanged)
    EVT_RADIOBUTTON(ID_ALONGSIDE, MainFrame::OnInstallTypeChanged)

    EVT_CHECKBOX(ID_MANUAL, MainFrame::OnManualCheck)
    EVT_BUTTON(ID_ADD, MainFrame::OnAddPartition)
    EVT_BUTTON(ID_EDIT, MainFrame::OnEditPartition)
    EVT_BUTTON(ID_DEL, MainFrame::OnDeletePartition)
    EVT_BUTTON(ID_RESET, MainFrame::OnResetPartitions)
    EVT_BUTTON(ID_GPARTED, MainFrame::OnGParted)

#ifdef HAS_DESKTOP_TAB
    EVT_CHECKBOX(ID_DESKTOP_LOCAL, MainFrame::OnDesktopLocal)
#endif

    EVT_TIMER(ID_DRAIN_TIMER, MainFrame::OnDrainTimer)
    EVT_TIMER(ID_PULSE_TIMER, MainFrame::OnPulseTimer)
wxEND_EVENT_TABLE()

namespace {
char *dupstr(const wxString &s) { return g_strdup(s.utf8_str()); }

void SetSizerEnabled(wxSizer *s, bool en) {
    if (!s) return;
    for (size_t i = 0; i < s->GetItemCount(); i++) {
        wxSizerItem *it = s->GetItem(i);
        if (it->GetWindow()) it->GetWindow()->Enable(en);
        if (it->GetSizer()) SetSizerEnabled(it->GetSizer(), en);
    }
}
} // namespace

MainFrame::MainFrame(AppData *app)
    : wxFrame(NULL, wxID_ANY, wxString::FromUTF8(get_loc("window_title", 0)),
              wxDefaultPosition, wxSize(980, 640)),
      app_(app), pulseValue_(0) {

    app_->kbd_layout_active = 0;
    app_->kbd_variant_active = 0;
    app_->grub_disk_active = -1;
    app_->privilege_doas = true;
    app_->manual_partitioning = false;
    app_->install_mode = INSTALL_MODE_ERASE;
#ifdef HAS_DESKTOP_TAB
    app_->selected_desktop = g_strdup("local");
#endif

    SetMinSize(wxSize(760, 520));
    Centre();

    wxBoxSizer *root = new wxBoxSizer(wxVERTICAL);
    notebook_ = new wxNotebook(this, wxID_ANY);
    root->Add(notebook_, wxSizerFlags(1).Expand().Border(wxALL, 10));

    notebook_->AddPage(BuildWelcome(), wxT("Welcome"));
    notebook_->AddPage(BuildInstallType(), wxT("Installation Type"));
    notebook_->AddPage(BuildPartitions(), wxT("Partitions"));
    notebook_->AddPage(BuildBootloader(), wxT("Bootloader"));
    notebook_->AddPage(BuildSystem(), wxT("System"));
#ifdef HAS_DESKTOP_TAB
    notebook_->AddPage(BuildDesktop(), wxT("Desktop"));
#endif
    notebook_->AddPage(BuildUsers(), wxT("Users"));
    notebook_->AddPage(BuildSecurity(), wxT("Security"));
    notebook_->AddPage(BuildInstall(), wxT("Install"));

    SetSizer(root);
    BuildNav();

    // Timers
    drainTimer_ = new wxTimer(this, ID_DRAIN_TIMER);
    pulseTimer_ = new wxTimer(this, ID_PULSE_TIMER);
    drainTimer_->Start(100);
    pulseTimer_->Start(80);

    // Localization + data
    UpdateUiLanguage();

    if (app_->is_efi)
        bootStatus_->SetLabel((app_->efi_target && strcmp(app_->efi_target, "i386-efi") == 0)
                              ? wxT("EFI System Detected (32-bit).")
                              : wxT("EFI System Detected (64-bit)."));
    else
        bootStatus_->SetLabel(wxT("BIOS/Legacy System Detected."));

    ScanDisks();
    if (!diskNames_.empty())
        diskChoice_->SetSelection(0);

    PopulateLocales();
    PopulateKbdLayouts();
    PopulateKbdVariants();
    PopulateCountries();

    // Default localization: United States -> en_US.UTF-8 / America/New_York / us
    {
        int usIdx = countryChoice_->FindString(wxT("United States"));
        if (usIdx == wxNOT_FOUND && countryChoice_->GetCount() > 0) usIdx = 0;
        if (usIdx != wxNOT_FOUND) countryChoice_->SetSelection(usIdx);
    }

    HandleDiskSelection();
    HandleCountrySelection();
    UpdateNavButtons();
}

MainFrame::~MainFrame() {
    drainTimer_->Stop();
    pulseTimer_->Stop();
}

// ---------------------------------------------------------------------
// Navigation sizer
// ---------------------------------------------------------------------
void MainFrame::BuildNav() {
    wxPanel *nav = new wxPanel(this);
    wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
    backBtn_ = new wxButton(nav, ID_BACK, wxT("Back"));
    nextBtn_ = new wxButton(nav, ID_NEXT, wxT("Next"));
    backBtn_->Enable(false);
    h->Add(backBtn_, wxSizerFlags().Border(wxALL, 8));
    h->AddStretchSpacer();
    h->Add(nextBtn_, wxSizerFlags().Border(wxALL, 8));
    nav->SetSizer(h);
    nav->SetMinSize(wxSize(-1, 40));

    GetSizer()->Add(nav, 0, wxEXPAND);
}

// ---------------------------------------------------------------------
// Page builders
// ---------------------------------------------------------------------
wxPanel *MainFrame::BuildWelcome() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    wxMemoryInputStream mem(logo_png, logo_png_len);
    wxImage img(mem, wxBITMAP_TYPE_PNG);
    if (img.IsOk()) {
        int target = 180;
        int h = target * img.GetHeight() / img.GetWidth();
        wxImage scaled = img.Scale(target, h, wxIMAGE_QUALITY_HIGH);
        wxStaticBitmap *bmp = new wxStaticBitmap(p, wxID_ANY, wxBitmap(scaled));
        v->Add(bmp, wxSizerFlags().Centre().Border(wxBOTTOM, 10));
    }

    welcomeTitle_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                     wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    wxFont titleFont = welcomeTitle_->GetFont();
    titleFont.SetPointSize(titleFont.GetPointSize() + 6);
    titleFont.SetWeight(wxFONTWEIGHT_BOLD);
    welcomeTitle_->SetFont(titleFont);
    v->Add(welcomeTitle_, wxSizerFlags().Centre().Border(wxALL, 4));

    welcomeBody_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                    wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    welcomeBody_->Wrap(520);
    v->Add(welcomeBody_, wxSizerFlags().Centre().Border(wxALL, 5));

    wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
    langLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    langChoice_ = new wxChoice(p, ID_LANG);
    langChoice_->Append(wxT("English"));
    langChoice_->Append(wxT("Español"));
    langChoice_->Append(wxT("日本語"));
    langChoice_->SetSelection(0);
    h->Add(langLabel_, wxSizerFlags().Border(wxRIGHT, 5));
    h->Add(langChoice_);
    v->Add(h, wxSizerFlags().Centre().Border(wxTOP, 10));

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildInstallType() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    installTypeTitle_ = new wxStaticText(p, wxID_ANY, wxT(""));
    installTypeTitle_->SetFont(installTypeTitle_->GetFont().Bold());
    v->Add(installTypeTitle_, wxSizerFlags().Border(wxBOTTOM, 8));

    cleanRadio_ = new wxRadioButton(p, ID_CLEAN, wxT(""), wxDefaultPosition,
                                    wxDefaultSize, wxRB_GROUP);
    alongRadio_ = new wxRadioButton(p, ID_ALONGSIDE, wxT(""));
    cleanRadio_->SetValue(true);
    v->Add(cleanRadio_, wxSizerFlags().Border(wxBOTTOM, 4));
    v->Add(alongRadio_, wxSizerFlags().Border(wxBOTTOM, 8));

    installTypeDesc_ = new wxStaticText(p, wxID_ANY, wxT(""));
    installTypeDesc_->Wrap(520);
    v->Add(installTypeDesc_, wxSizerFlags().Border(wxTOP, 5));

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildPartitions() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *hDisk = new wxBoxSizer(wxHORIZONTAL);
    diskTitle_ = new wxStaticText(p, wxID_ANY, wxT(""));
    diskChoice_ = new wxChoice(p, ID_DISK);
    gpartedBtn_ = new wxButton(p, ID_GPARTED, wxT(""));
    hDisk->Add(diskTitle_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hDisk->Add(diskChoice_, wxSizerFlags(1).Border(wxRIGHT, 5));
    hDisk->Add(gpartedBtn_);
    v->Add(hDisk, wxSizerFlags().Expand().Border(wxBOTTOM, 5));

    diskInfo_ = new wxStaticText(p, wxID_ANY, wxT(""));
    diskInfo_->Wrap(520);
    v->Add(diskInfo_, wxSizerFlags().Border(wxBOTTOM, 5));

    // Existing partitions (read-only)
    existingBox_ = new wxStaticBoxSizer(wxVERTICAL, p, wxT(""));
    existingList_ = new wxListCtrl(existingBox_->GetStaticBox(), wxID_ANY,
                                   wxDefaultPosition, wxSize(-1, 120),
                                   wxLC_REPORT | wxLC_SINGLE_SEL);
    existingList_->AppendColumn(wxT("Device"));
    existingList_->AppendColumn(wxT("Size"));
    existingList_->AppendColumn(wxT("FS Type"));
    for (int c = 0; c < 3; c++) existingList_->SetColumnWidth(c, 160);
    existingBox_->Add(existingList_, wxSizerFlags(1).Expand().Border(wxALL, 5));
    v->Add(existingBox_, wxSizerFlags(1).Expand().Border(wxBOTTOM, 5));

    manualCheck_ = new wxCheckBox(p, ID_MANUAL, wxT(""));
    v->Add(manualCheck_, wxSizerFlags().Border(wxBOTTOM, 5));

    // Installation partitions (editable)
    installBox_ = new wxStaticBoxSizer(wxVERTICAL, p, wxT(""));
    wxBoxSizer *hPm = new wxBoxSizer(wxHORIZONTAL);
    partMountLabel_ = new wxStaticText(installBox_->GetStaticBox(), wxID_ANY, wxT(""));
    addBtn_ = new wxButton(installBox_->GetStaticBox(), ID_ADD, wxT("Add"));
    editBtn_ = new wxButton(installBox_->GetStaticBox(), ID_EDIT, wxT("Edit"));
    delBtn_ = new wxButton(installBox_->GetStaticBox(), ID_DEL, wxT("Delete"));
    resetBtn_ = new wxButton(installBox_->GetStaticBox(), ID_RESET, wxT("Reset"));
    hPm->Add(partMountLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hPm->Add(addBtn_, wxSizerFlags().Border(wxRIGHT, 3));
    hPm->Add(editBtn_, wxSizerFlags().Border(wxRIGHT, 3));
    hPm->Add(delBtn_, wxSizerFlags().Border(wxRIGHT, 3));
    hPm->Add(resetBtn_);
    installBox_->Add(hPm, wxSizerFlags().Expand().Border(wxLEFT | wxRIGHT | wxTOP, 5));

    mountList_ = new wxListCtrl(installBox_->GetStaticBox(), wxID_ANY,
                                wxDefaultPosition, wxSize(-1, 120),
                                wxLC_REPORT | wxLC_SINGLE_SEL);
    mountList_->AppendColumn(wxT("Device"));
    mountList_->AppendColumn(wxT("Mount Point"));
    mountList_->AppendColumn(wxT("FS Type"));
    mountList_->AppendColumn(wxT("Format?"));
    mountList_->AppendColumn(wxT("Encrypted?"));
    for (int c = 0; c < 5; c++) mountList_->SetColumnWidth(c, 120);
    installBox_->Add(mountList_, wxSizerFlags(1).Expand().Border(wxALL, 5));
    v->Add(installBox_, wxSizerFlags(1).Expand());

    SetSizerEnabled(installBox_, false);

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildBootloader() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    bootStatus_ = new wxStaticText(p, wxID_ANY, wxT("Detecting firmware..."));
    v->Add(bootStatus_, wxSizerFlags().Border(wxBOTTOM, 15));

    wxBoxSizer *h = new wxBoxSizer(wxHORIZONTAL);
    grubLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    grubChoice_ = new wxChoice(p, ID_GRUB);
    h->Add(grubLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 10));
    h->Add(grubChoice_, wxSizerFlags(1));
    v->Add(h, wxSizerFlags().Expand());

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildSystem() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    wxBoxSizer *hHost = new wxBoxSizer(wxHORIZONTAL);
    hostnameLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    hostnameEntry_ = new wxTextCtrl(p, ID_HOSTNAME, wxT("neko-void"));
    hostnameEntry_->Bind(wxEVT_CHAR, &MainFrame::OnHostnameChar, this);
    hHost->Add(hostnameLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hHost->Add(hostnameEntry_, wxSizerFlags(1));
    v->Add(hHost, wxSizerFlags().Expand().Border(wxBOTTOM, 10));

    wxBoxSizer *hCountry = new wxBoxSizer(wxHORIZONTAL);
    countryLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    countryChoice_ = new wxChoice(p, ID_COUNTRY);
    hCountry->Add(countryLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hCountry->Add(countryChoice_, wxSizerFlags(1));
    v->Add(hCountry, wxSizerFlags().Expand().Border(wxBOTTOM, 10));

    wxBoxSizer *hLoc = new wxBoxSizer(wxHORIZONTAL);
    localeLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    localeChoice_ = new wxChoice(p, wxID_ANY);
    hLoc->Add(localeLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hLoc->Add(localeChoice_, wxSizerFlags(1));
    v->Add(hLoc, wxSizerFlags().Expand().Border(wxBOTTOM, 10));

    wxBoxSizer *hTz = new wxBoxSizer(wxHORIZONTAL);
    regionLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    tzAreaChoice_ = new wxChoice(p, ID_TZ_AREA);
    cityLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    tzCityChoice_ = new wxChoice(p, ID_TZ_CITY);
    hTz->Add(regionLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hTz->Add(tzAreaChoice_, wxSizerFlags(1).Border(wxRIGHT, 5));
    hTz->Add(cityLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hTz->Add(tzCityChoice_, wxSizerFlags(1));
    v->Add(hTz, wxSizerFlags().Expand().Border(wxBOTTOM, 10));

    const char *areas[] = { "Africa", "America", "Antarctica", "Arctic", "Asia",
                            "Atlantic", "Australia", "Europe", "Indian", "Pacific", NULL };
    for (int i = 0; areas[i]; i++) tzAreaChoice_->Append(wxString::FromUTF8(areas[i]));

    wxBoxSizer *hKbd = new wxBoxSizer(wxHORIZONTAL);
    kbdLayoutLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    kbdLayoutChoice_ = new wxChoice(p, ID_KBD_LAYOUT);
    hKbd->Add(kbdLayoutLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hKbd->Add(kbdLayoutChoice_, wxSizerFlags(1));
    v->Add(hKbd, wxSizerFlags().Expand().Border(wxBOTTOM, 10));

    wxBoxSizer *hKvar = new wxBoxSizer(wxHORIZONTAL);
    kbdVariantLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    kbdVariantChoice_ = new wxChoice(p, ID_KBD_VARIANT);
    hKvar->Add(kbdVariantLabel_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 5));
    hKvar->Add(kbdVariantChoice_, wxSizerFlags(1));
    v->Add(hKvar, wxSizerFlags().Expand());

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

#ifdef HAS_DESKTOP_TAB
wxPanel *MainFrame::BuildDesktop() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    desktopTitle_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                     wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    desktopTitle_->SetFont(desktopTitle_->GetFont().Bold());
    v->Add(desktopTitle_, wxSizerFlags().Centre().Border(wxBOTTOM, 2));

    desktopDesc_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                    wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    v->Add(desktopDesc_, wxSizerFlags().Centre().Border(wxBOTTOM, 10));

    // "local (default)"
    desktopLocal_ = new wxCheckBox(p, ID_DESKTOP_LOCAL, wxT(""));
    desktopLocal_->SetValue(true);
    v->Add(desktopLocal_, wxSizerFlags().Border(wxBOTTOM, 6));

    wxStaticLine *line = new wxStaticLine(p);
    v->Add(line, wxSizerFlags().Expand().Border(wxBOTTOM, 8));

    desktopSelPanel_ = new wxPanel(p);
    wxBoxSizer *selV = new wxBoxSizer(wxVERTICAL);

    struct DEntry { const char *id; const char *label; const char *descKey; };
    const DEntry entries[] = {
        { "xfce",  "Xfce",       "d_xfce_desc" },
        { "niri",  "Niri",       "d_niri_desc" },
        { "kde",   "KDE Plasma", "d_kde_desc" },
        { "icewm", "IceWM",      "d_icewm_desc" },
        { "mate",  "MATE",       "d_mate_desc" },
        { "labwc", "Labwc",      "d_labwc_desc" },
        { "lxqt",  "LXQt",       "d_lxqt_desc" },
        { "i3",    "I3",         "d_i3_desc" },
        { "jwm",   "JWM",        "d_jwm_desc" },
    };

    for (size_t i = 0; i < sizeof(entries) / sizeof(entries[0]); i++) {
        int id = ID_DESKTOP_FIRST + (int)i;
        wxCheckBox *chk = new wxCheckBox(desktopSelPanel_, id, wxString::FromUTF8(entries[i].label));
        wxStaticText *dsc = new wxStaticText(desktopSelPanel_, wxID_ANY,
                                             wxString::FromUTF8(get_loc(entries[i].descKey, app_->current_lang)));
        dsc->Wrap(420);
        wxBoxSizer *row = new wxBoxSizer(wxHORIZONTAL);
        row->Add(chk, wxSizerFlags().CentreVertical().Border(wxRIGHT, 10));
        row->Add(dsc, wxSizerFlags(1).CentreVertical());
        selV->Add(row, wxSizerFlags().Expand().Border(wxBOTTOM, 6));

        DesktopOpt opt;
        opt.chk = chk;
        opt.desc = dsc;
        opt.id = entries[i].id;
        opt.descKey = entries[i].descKey;
        desktopOpts_.push_back(opt);

        chk->Bind(wxEVT_CHECKBOX, &MainFrame::OnDesktopSelected, this);
    }

    desktopSelPanel_->SetSizer(selV);
    desktopSelPanel_->Enable(false);
    v->Add(desktopSelPanel_, wxSizerFlags(1).Expand());

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}
#else
wxPanel *MainFrame::BuildDesktop() { return NULL; }
#endif

wxPanel *MainFrame::BuildUsers() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    auto addInput = [&](wxStaticText **lbl, PasswordField **ent, const wxString &placeholder) {
        *lbl = new wxStaticText(p, wxID_ANY, wxT(""));
        *ent = new PasswordField(p, placeholder);
        v->Add(*lbl, wxSizerFlags().Border(wxLEFT, 2));
        v->Add(*ent, wxSizerFlags().Expand().Border(wxBOTTOM, 8));
    };

    fullnameLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    fullnameEntry_ = new wxTextCtrl(p, wxID_ANY);
    v->Add(fullnameLabel_, wxSizerFlags().Border(wxLEFT, 2));
    v->Add(fullnameEntry_, wxSizerFlags().Expand().Border(wxBOTTOM, 8));

    usernameLabel_ = new wxStaticText(p, wxID_ANY, wxT(""));
    usernameEntry_ = new wxTextCtrl(p, ID_USERNAME);
    usernameEntry_->Bind(wxEVT_CHAR, &MainFrame::OnUsernameChar, this);
    v->Add(usernameLabel_, wxSizerFlags().Border(wxLEFT, 2));
    v->Add(usernameEntry_, wxSizerFlags().Expand().Border(wxBOTTOM, 8));

    addInput(&userPassLabel_, &userPassEntry_, wxT("Password"));
    addInput(&userConfirmLabel_, &userPassConfirmEntry_, wxT("Confirm"));
    addInput(&rootPassLabel_, &rootPassEntry_, wxT("Root Password"));

    autologinCheck_ = new wxCheckBox(p, wxID_ANY, wxT(""));
    v->Add(autologinCheck_, wxSizerFlags().Centre().Border(wxTOP, 10));

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildSecurity() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    privTitle_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                  wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    privTitle_->SetFont(privTitle_->GetFont().Bold());
    v->Add(privTitle_, wxSizerFlags().Centre().Border(wxBOTTOM, 5));

    privDesc_ = new wxStaticText(p, wxID_ANY, wxT(""), wxDefaultPosition,
                                 wxDefaultSize, wxALIGN_CENTRE_HORIZONTAL);
    v->Add(privDesc_, wxSizerFlags().Centre().Border(wxBOTTOM, 20));

    sudoRadio_ = new wxRadioButton(p, ID_SUDO, wxT("sudo"), wxDefaultPosition,
                                   wxDefaultSize, wxRB_GROUP);
    privSudoDesc_ = new wxStaticText(p, wxID_ANY, wxT(""));
    privSudoDesc_->Wrap(420);
    wxBoxSizer *r1 = new wxBoxSizer(wxHORIZONTAL);
    r1->Add(sudoRadio_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 10));
    r1->Add(privSudoDesc_, wxSizerFlags(1).CentreVertical());
    v->Add(r1, wxSizerFlags().Expand().Border(wxBOTTOM, 12));

    doasRadio_ = new wxRadioButton(p, ID_DOAS, wxT("doas"));
    privDoasDesc_ = new wxStaticText(p, wxID_ANY, wxT(""));
    privDoasDesc_->Wrap(420);
    wxBoxSizer *r2 = new wxBoxSizer(wxHORIZONTAL);
    r2->Add(doasRadio_, wxSizerFlags().CentreVertical().Border(wxRIGHT, 10));
    r2->Add(privDoasDesc_, wxSizerFlags(1).CentreVertical());
    v->Add(r2, wxSizerFlags().Expand());

    doasRadio_->SetValue(true);

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

wxPanel *MainFrame::BuildInstall() {
    wxPanel *p = new wxPanel(notebook_);
    wxBoxSizer *v = new wxBoxSizer(wxVERTICAL);

    console_ = new wxTextCtrl(p, wxID_ANY, wxT(""),
                              wxDefaultPosition, wxSize(-1, 200),
                              wxTE_MULTILINE | wxTE_READONLY | wxTE_RICH);
    wxFont mono(10, wxFONTFAMILY_TELETYPE, wxFONTSTYLE_NORMAL, wxFONTWEIGHT_NORMAL);
    console_->SetFont(mono);
    console_->SetBackgroundColour(wxColour(0x1e, 0x1e, 0x1e));
    console_->SetForegroundColour(wxColour(0xe0, 0xe0, 0xe0));
    v->Add(console_, wxSizerFlags(1).Expand().Border(wxBOTTOM, 8));

    gauge_ = new wxGauge(p, wxID_ANY, 100);
    v->Add(gauge_, wxSizerFlags().Expand().Border(wxBOTTOM, 8));

    installBtn_ = new wxButton(p, ID_INSTALL, wxT(""));
    installBtn_->SetMinSize(wxSize(-1, 40));
    installBtn_->Enable(false);
    v->Add(installBtn_, wxSizerFlags().Expand());

    wxBoxSizer *pageOuter = new wxBoxSizer(wxVERTICAL);
    pageOuter->Add(v, 1, wxEXPAND | wxALL, 18);
    p->SetSizer(pageOuter);
    return p;
}

// ---------------------------------------------------------------------
// Data population
// ---------------------------------------------------------------------
void MainFrame::ScanDisks() {
    diskChoice_->Clear();
    grubChoice_->Clear();
    diskNames_.clear();

    DIR *dp = opendir("/sys/block");
    if (!dp) return;
    struct dirent *entry;
    while ((entry = readdir(dp)) != NULL) {
        const char *n = entry->d_name;
        if (strncmp(n, "sd", 2) == 0 || strncmp(n, "vd", 2) == 0 ||
            strncmp(n, "nvme", 4) == 0 || strncmp(n, "hd", 2) == 0 ||
            strncmp(n, "mmcblk", 6) == 0) {
            char path[512];
            snprintf(path, sizeof(path), "/sys/block/%s/size", n);
            unsigned long long sectors = 0;
            FILE *f = fopen(path, "r");
            if (f) { fscanf(f, "%llu", &sectors); fclose(f); }
            double gb = (sectors * 512.0) / (1024.0 * 1024.0 * 1024.0);

            diskNames_.push_back(n);
            diskChoice_->Append(wxString::Format(wxT("%s  (%.1f GB)"), n, gb));
            grubChoice_->Append(wxString::Format(wxT("/dev/%s  (%.1f GB)"), n, gb));
        }
    }
    closedir(dp);
}

void MainFrame::RefreshPartitionList() {
    mountList_->DeleteAllItems();
    for (GSList *l = app_->part_config_list; l; l = l->next) {
        PartitionConfig *c = (PartitionConfig *)l->data;
        long idx = mountList_->InsertItem(mountList_->GetItemCount(),
                                          wxString::FromUTF8(c->device));
        mountList_->SetItem(idx, 1, wxString::FromUTF8(c->mountpoint));
        mountList_->SetItem(idx, 2, wxString::FromUTF8(c->fstype));
        mountList_->SetItem(idx, 3, c->format ? wxT("YES") : wxT("NO"));
        mountList_->SetItem(idx, 4, c->encrypt ? wxT("LUKS") : wxT("-"));
    }
}

void MainFrame::RefreshExistingPartitions() {
    existingList_->DeleteAllItems();
    if (!app_->selected_disk) return;

    char cmd[256];
    snprintf(cmd, sizeof(cmd),
             "lsblk -rn -o NAME,SIZE,FSTYPE /dev/%s 2>/dev/null | grep -E '^%s[0-9]'",
             app_->selected_disk, app_->selected_disk);
    FILE *fp = popen(cmd, "r");
    if (!fp) return;
    char line[256];
    while (fgets(line, sizeof(line), fp)) {
        char name[64] = {0}, size[32] = {0}, fstype[32] = {0};
        if (sscanf(line, "%63s %31s %31s", name, size, fstype) >= 2) {
            if (strlen(fstype) == 0 || strcmp(fstype, "-") == 0 || strlen(fstype) > 16)
                strcpy(fstype, "-");
            char dev_path[128];
            snprintf(dev_path, sizeof(dev_path), "/dev/%s", name);
            long idx = existingList_->InsertItem(existingList_->GetItemCount(),
                                                 wxString::FromUTF8(dev_path));
            existingList_->SetItem(idx, 1, wxString::FromUTF8(size));
            existingList_->SetItem(idx, 2, wxString::FromUTF8(fstype));
        }
    }
    pclose(fp);
}

void MainFrame::PopulateCountries() {
    countryChoice_->Clear();
    int count = 0;
    const CountryInfo *countries = get_country_list(&count);
    for (int i = 0; i < count; i++)
        countryChoice_->Append(wxString::FromUTF8(countries[i].name));
}

void MainFrame::PopulateLocales() {
    localeChoice_->Clear();
    FILE *fp = fopen("/etc/default/libc-locales", "r");
    int count = 0, defaultIdx = 0;
    if (fp) {
        char line[256];
        while (fgets(line, sizeof(line), fp)) {
            if (line[0] == '\n' || line[0] == '\0') continue;
            if (!strstr(line, "UTF-8")) continue;
            char *p = line;
            while (*p == '#' || *p == ' ' || *p == '\t') p++;
            char locale_name[128];
            int i = 0;
            while (*p && *p != ' ' && *p != '\t' && *p != '\n' && i < (int)sizeof(locale_name) - 1)
                locale_name[i++] = *p++;
            locale_name[i] = '\0';
            if (i == 0) continue;
            localeChoice_->Append(wxString::FromUTF8(locale_name));
            if (strcmp(locale_name, "en_US.UTF-8") == 0) defaultIdx = count;
            count++;
        }
        fclose(fp);
    }
    if (count == 0) {
        localeChoice_->Append(wxT("en_US.UTF-8"));
        defaultIdx = 0;
    }
    localeChoice_->SetSelection(defaultIdx);
}

void MainFrame::PopulateKbdLayouts() {
    kbdLayoutChoice_->Clear();
    int count = 0;
    const KbdLayout *layouts = get_keyboard_layouts(&count);
    for (int i = 0; i < count; i++)
        kbdLayoutChoice_->Append(wxString::FromUTF8(layouts[i].name));
    kbdLayoutChoice_->SetSelection(0);
}

void MainFrame::PopulateKbdVariants() {
    kbdVariantChoice_->Clear();
    int count = 0;
    const KbdLayout *layouts = get_keyboard_layouts(&count);
    int active = kbdLayoutChoice_->GetSelection();
    if (active < 0 || active >= count) active = 0;
    const KbdVariant *vars = layouts[active].variants;
    for (int i = 0; vars[i].name != NULL; i++)
        kbdVariantChoice_->Append(wxString::FromUTF8(vars[i].name));
    kbdVariantChoice_->SetSelection(0);
}

void MainFrame::PopulateTzCities() {
    tzCityChoice_->Clear();
    wxString area = tzAreaChoice_->GetStringSelection();
    if (area.empty()) return;
    g_free(app_->tz_area);
    app_->tz_area = dupstr(area);

    wxString path = wxT("/usr/share/zoneinfo/") + area;
    DIR *d = opendir(path.utf8_str());
    if (d) {
        std::vector<std::string> cities;
        struct dirent *dir;
        while ((dir = readdir(d)) != NULL) {
            if (dir->d_name[0] != '.') cities.push_back(dir->d_name);
        }
        closedir(d);
        std::sort(cities.begin(), cities.end());
        for (const auto &c : cities) tzCityChoice_->Append(wxString::FromUTF8(c));
    }
    if (tzCityChoice_->GetCount() > 0) {
        tzCityChoice_->SetSelection(0);
        g_free(app_->tz_city);
        app_->tz_city = dupstr(tzCityChoice_->GetString(0));
    } else {
        g_free(app_->tz_city);
        app_->tz_city = NULL;
    }
}

// ---------------------------------------------------------------------
// Selection handling
// ---------------------------------------------------------------------
void MainFrame::HandleDiskSelection() {
    int sel = diskChoice_->GetSelection();
    if (sel < 0 || sel >= (int)diskNames_.size()) return;
    const std::string &disk = diskNames_[sel];

    g_free(app_->selected_disk);
    app_->selected_disk = g_strdup(disk.c_str());

    if (app_->detected_ntfs_partition) {
        g_free(app_->detected_ntfs_partition);
        app_->detected_ntfs_partition = NULL;
    }

    char *ntfs = find_ntfs_partition(app_->selected_disk);
    if (ntfs) {
        app_->detected_ntfs_partition = ntfs;
        wxString msg = wxString::Format(wxT("Windows (NTFS) partition detected on %s."),
                                        wxString::FromUTF8(ntfs));
        wxMessageDialog dlg(this, msg, wxT("Warning"), wxOK | wxICON_WARNING);
        dlg.ShowModal();
        populate_defaults(app_, app_->selected_disk);
    } else {
        populate_defaults(app_, app_->selected_disk);
    }
    RefreshExistingPartitions();
    RefreshPartitionList();
}

void MainFrame::HandleCountrySelection() {
    wxString country = countryChoice_->GetStringSelection();
    if (country.empty()) return;
    g_free(app_->country_name);
    app_->country_name = dupstr(country);

    const CountryInfo *info = find_country_by_name(country.utf8_str());
    if (!info) return;

    for (unsigned i = 0; i < localeChoice_->GetCount(); i++) {
        if (localeChoice_->GetString(i) == wxString::FromUTF8(info->locale)) {
            localeChoice_->SetSelection(i);
            break;
        }
    }

    for (unsigned i = 0; i < tzAreaChoice_->GetCount(); i++) {
        if (tzAreaChoice_->GetString(i) == wxString::FromUTF8(info->tz_area)) {
            tzAreaChoice_->SetSelection(i);
            break;
        }
    }
    PopulateTzCities();

    int cityIdx = tzCityChoice_->FindString(wxString::FromUTF8(info->tz_city));
    if (cityIdx == wxNOT_FOUND) {
        const char *slash = strrchr(info->tz_city, '/');
        cityIdx = tzCityChoice_->FindString(wxString::FromUTF8(slash ? slash + 1 : info->tz_city));
    }
    if (cityIdx != wxNOT_FOUND) {
        tzCityChoice_->SetSelection(cityIdx);
        g_free(app_->tz_city);
        app_->tz_city = dupstr(tzCityChoice_->GetString(cityIdx));
    }

    if (info->x11_layout) {
        const KbdLayout *kbd = find_keyboard_layout(info->x11_layout);
        if (kbd) {
            int count = 0;
            const KbdLayout *all = get_keyboard_layouts(&count);
            for (int i = 0; i < count; i++) {
                if (&all[i] == kbd) { kbdLayoutChoice_->SetSelection(i); break; }
            }
            PopulateKbdVariants();
            app_->kbd_layout_active = kbdLayoutChoice_->GetSelection();
            app_->kbd_variant_active = 0;
        }
    }
}

// ---------------------------------------------------------------------
// Localization
// ---------------------------------------------------------------------
void MainFrame::UpdateUiLanguage() {
    int lang = app_->current_lang;
    SetTitle(wxString::FromUTF8(get_loc("window_title", lang)));

    if (langLabel_) langLabel_->SetLabel(wxString::FromUTF8(get_loc("language", lang)));
    if (welcomeTitle_) welcomeTitle_->SetLabel(wxString::FromUTF8(get_loc("welcome_title", lang)));
    if (welcomeBody_) welcomeBody_->SetLabel(wxString::FromUTF8(get_loc("welcome_body", lang)));

    if (installTypeTitle_) installTypeTitle_->SetLabel(wxString::FromUTF8(get_loc("install_type_title", lang)));
    if (cleanRadio_) cleanRadio_->SetLabel(wxString::FromUTF8(get_loc("clean_install", lang)));
    if (alongRadio_) alongRadio_->SetLabel(wxString::FromUTF8(get_loc("install_alongside", lang)));
    if (installTypeDesc_) {
        bool clean = cleanRadio_ && cleanRadio_->GetValue();
        installTypeDesc_->SetLabel(wxString::FromUTF8(get_loc(clean ? "clean_install_desc" : "alongside_desc", lang)));
    }

    if (diskTitle_) diskTitle_->SetLabel(wxString::FromUTF8(get_loc("disk_title", lang)));
    if (diskInfo_) diskInfo_->SetLabel(wxString::FromUTF8(get_loc("disk_info", lang)));
    if (gpartedBtn_) gpartedBtn_->SetLabel(wxString::FromUTF8(get_loc("btn_gparted", lang)));
    if (existingBox_) existingBox_->GetStaticBox()->SetLabel(wxString::FromUTF8(get_loc("existing_parts", lang)));
    if (manualCheck_) manualCheck_->SetLabel(wxString::FromUTF8(get_loc("manual_partitioning", lang)));
    if (installBox_) installBox_->GetStaticBox()->SetLabel(wxString::FromUTF8(get_loc("install_parts", lang)));
    if (partMountLabel_) partMountLabel_->SetLabel(wxString::FromUTF8(get_loc("part_mount", lang)));
    if (addBtn_) addBtn_->SetLabel(wxString::FromUTF8(get_loc("btn_add", lang)));
    if (editBtn_) editBtn_->SetLabel(wxString::FromUTF8(get_loc("btn_edit", lang)));
    if (delBtn_) delBtn_->SetLabel(wxString::FromUTF8(get_loc("btn_del", lang)));
    if (resetBtn_) resetBtn_->SetLabel(wxString::FromUTF8(get_loc("btn_reset", lang)));

    if (grubLabel_) grubLabel_->SetLabel(wxString::FromUTF8(get_loc("grub_install", lang)));

    if (hostnameLabel_) hostnameLabel_->SetLabel(wxString::FromUTF8(get_loc("hostname", lang)));
    if (countryLabel_) countryLabel_->SetLabel(wxString::FromUTF8(get_loc("country", lang)));
    if (localeLabel_) localeLabel_->SetLabel(wxString::FromUTF8(get_loc("locale", lang)));
    if (regionLabel_) regionLabel_->SetLabel(wxString::FromUTF8(get_loc("region", lang)));
    if (cityLabel_) cityLabel_->SetLabel(wxString::FromUTF8(get_loc("city", lang)));
    if (kbdLayoutLabel_) kbdLayoutLabel_->SetLabel(wxString::FromUTF8("Keyboard Layout:"));
    if (kbdVariantLabel_) kbdVariantLabel_->SetLabel(wxString::FromUTF8("Variant:"));

    if (fullnameLabel_) fullnameLabel_->SetLabel(wxString::FromUTF8(get_loc("fullname", lang)));
    if (usernameLabel_) usernameLabel_->SetLabel(wxString::FromUTF8(get_loc("username", lang)));
    if (userPassLabel_) userPassLabel_->SetLabel(wxString::FromUTF8(get_loc("password", lang)));
    if (userConfirmLabel_) userConfirmLabel_->SetLabel(wxString::FromUTF8(get_loc("confirm", lang)));
    if (rootPassLabel_) rootPassLabel_->SetLabel(wxString::FromUTF8(get_loc("root_pass", lang)));
    if (autologinCheck_) autologinCheck_->SetLabel(wxString::FromUTF8(get_loc("autologin", lang)));

    if (privTitle_) privTitle_->SetLabel(wxString::FromUTF8(get_loc("priv_title", lang)));
    if (privDesc_) privDesc_->SetLabel(wxString::FromUTF8(get_loc("priv_desc", lang)));
    if (sudoRadio_) sudoRadio_->SetLabel(wxString::FromUTF8(get_loc("priv_sudo_label", lang)));
    if (doasRadio_) doasRadio_->SetLabel(wxString::FromUTF8(get_loc("priv_doas_label", lang)));
    if (privSudoDesc_) privSudoDesc_->SetLabel(wxString::FromUTF8(get_loc("priv_sudo_desc", lang)));
    if (privDoasDesc_) privDoasDesc_->SetLabel(wxString::FromUTF8(get_loc("priv_doas_desc", lang)));

#ifdef HAS_DESKTOP_TAB
    if (desktopTitle_) desktopTitle_->SetLabel(wxString::FromUTF8(get_loc("desktop_title", lang)));
    if (desktopDesc_) desktopDesc_->SetLabel(wxString::FromUTF8(get_loc("desktop_desc", lang)));
    if (desktopLocal_) desktopLocal_->SetLabel(wxString::FromUTF8(get_loc("d_local_label", lang)));
    for (auto &o : desktopOpts_)
        o.desc->SetLabel(wxString::FromUTF8(get_loc(o.descKey.c_str(), lang)));
#endif

    if (!app_->installing) installBtn_->SetLabel(wxString::FromUTF8(get_loc("install_btn", lang)));
    backBtn_->SetLabel(wxString::FromUTF8(get_loc("back", lang)));
    nextBtn_->SetLabel(wxString::FromUTF8(get_loc("next", lang)));

    notebook_->SetPageText(0, wxString::FromUTF8(get_loc("tab_welcome", lang)));
    notebook_->SetPageText(1, wxString::FromUTF8(get_loc("tab_install_type", lang)));
    notebook_->SetPageText(2, wxString::FromUTF8(get_loc("tab_partitions", lang)));
    notebook_->SetPageText(3, wxString::FromUTF8(get_loc("tab_bootloader", lang)));
    notebook_->SetPageText(4, wxString::FromUTF8(get_loc("tab_system", lang)));
#ifdef HAS_DESKTOP_TAB
    notebook_->SetPageText(5, wxString::FromUTF8(get_loc("tab_desktop", lang)));
    notebook_->SetPageText(6, wxString::FromUTF8(get_loc("tab_users", lang)));
    notebook_->SetPageText(7, wxString::FromUTF8(get_loc("tab_privilege", lang)));
    notebook_->SetPageText(8, wxString::FromUTF8(get_loc("tab_install", lang)));
#else
    notebook_->SetPageText(5, wxString::FromUTF8(get_loc("tab_users", lang)));
    notebook_->SetPageText(6, wxString::FromUTF8(get_loc("tab_privilege", lang)));
    notebook_->SetPageText(7, wxString::FromUTF8(get_loc("tab_install", lang)));
#endif

    Layout();
}

// ---------------------------------------------------------------------
// Validation / navigation
// ---------------------------------------------------------------------
const char *MainFrame::ValidateTab(int page) {
    int lang = app_->current_lang;
    switch (page) {
    case 2:
        if (!app_->selected_disk || strlen(app_->selected_disk) == 0)
            return get_loc("val_select_disk", lang);
        {
            bool hasRoot = false;
            for (GSList *l = app_->part_config_list; l; l = l->next) {
                PartitionConfig *c = (PartitionConfig *)l->data;
                if (c->mountpoint && strcmp(c->mountpoint, "/") == 0) hasRoot = true;
            }
            if (!hasRoot) return get_loc("val_no_root", lang);
        }
        break;
    case 3:
        if (grubChoice_->GetSelection() < 0) return get_loc("val_grub_disk", lang);
        break;
    case 4: {
        wxString h = hostnameEntry_->GetValue();
        if (h.Trim().empty()) return get_loc("val_hostname", lang);
    } break;
#ifdef HAS_DESKTOP_TAB
    case 5:
        break;
    case 6:
#else
    case 5:
#endif
    {
        wxString u = usernameEntry_->GetValue();
        wxString pw = userPassEntry_->GetValue();
        wxString cf = userPassConfirmEntry_->GetValue();
        wxString rp = rootPassEntry_->GetValue();
        if (u.empty()) return get_loc("val_username", lang);
        if (pw.empty()) return get_loc("val_password", lang);
        if (pw != cf) return get_loc("val_password_match", lang);
        if (rp.empty()) return get_loc("val_root_pass", lang);
    } break;
    default:
        break;
    }
    return NULL;
}

void MainFrame::UpdateNavButtons() {
    int page = notebook_->GetSelection();
    if (app_->installing) {
        backBtn_->Enable(false);
        nextBtn_->Show(false);
        installBtn_->Enable(false);
        Layout();
        return;
    }
    backBtn_->Enable(page > 0);
    int total = notebook_->GetPageCount();
    bool onLast = (page >= total - 1);
    nextBtn_->Show(!onLast);

    if (onLast) {
        bool allValid = true;
        for (int i = 2; i <= LAST_EDITABLE; i++) {
            if (ValidateTab(i) != NULL) { allValid = false; break; }
        }
        installBtn_->Enable(allValid);
    } else {
        installBtn_->Enable(false);
    }
    Layout();
}

void MainFrame::CollectInstallValues() {
    g_free(app_->ui_hostname);
    app_->ui_hostname = dupstr(hostnameEntry_->GetValue());
    g_free(app_->ui_locale);
    app_->ui_locale = dupstr(localeChoice_->GetStringSelection());
    g_free(app_->ui_root_pass);
    app_->ui_root_pass = dupstr(rootPassEntry_->GetValue());
    g_free(app_->ui_user_login);
    app_->ui_user_login = dupstr(usernameEntry_->GetValue());
    g_free(app_->ui_user_pass);
    app_->ui_user_pass = dupstr(userPassEntry_->GetValue());
    g_free(app_->ui_user_fullname);
    app_->ui_user_fullname = dupstr(fullnameEntry_->GetValue());
    app_->ui_autologin = autologinCheck_->GetValue();
    app_->privilege_doas = doasRadio_->GetValue();
    app_->grub_disk_active = grubChoice_->GetSelection();
}

void MainFrame::OnInstallFinished() {
    if (app_->finished) return;
    app_->finished = true;

    backBtn_->Enable(false);
    nextBtn_->Show(false);
    nextBtn_->Enable(false);
    for (int i = 0; i < TAB_COUNT - 1; i++)
        notebook_->GetPage(i)->Enable(false);

    installBtn_->SetLabel(wxString::FromUTF8(get_loc("reboot_btn", app_->current_lang)));
    installBtn_->Enable(true);
    Layout();

    wxDialog dlg(this, wxID_ANY, wxString::FromUTF8(get_loc("success_title", app_->current_lang)),
                 wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
    wxBoxSizer *s = new wxBoxSizer(wxVERTICAL);
    wxStaticText *body = new wxStaticText(&dlg, wxID_ANY,
                                          wxString::FromUTF8(get_loc("success_body", app_->current_lang)));
    body->Wrap(420);
    s->Add(body, wxSizerFlags().Centre().Border(wxALL, 16));
    wxBoxSizer *b = new wxBoxSizer(wxHORIZONTAL);
    wxButton *reboot = new wxButton(&dlg, wxID_ANY,
                                    wxString::FromUTF8(get_loc("success_reboot", app_->current_lang)));
    wxButton *close = new wxButton(&dlg, wxID_ANY, wxT("Close"));
    b->Add(reboot, wxSizerFlags().Border(wxRIGHT, 8));
    b->Add(close);
    s->Add(b, wxSizerFlags().Centre().Border(wxBOTTOM, 16));
    dlg.SetSizerAndFit(s);
    dlg.CentreOnParent();
    reboot->Bind(wxEVT_BUTTON, [&dlg](wxCommandEvent &) { dlg.EndModal(wxID_OK); system("reboot"); });
    close->Bind(wxEVT_BUTTON, [&dlg](wxCommandEvent &) { dlg.EndModal(wxID_CANCEL); });
    dlg.ShowModal();
}

// ---------------------------------------------------------------------
// Partition dialog helpers
// ---------------------------------------------------------------------
void MainFrame::ShowPartitionDialog(PartitionConfig *edit) {
    if (!app_->selected_disk) {
        wxMessageDialog dlg(this, wxT("Please select a disk first!"),
                            wxT("Error"), wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return;
    }
    PartitionDialog dlg(this, app_, edit);
    if (dlg.ShowModal() == wxID_OK)
        RefreshPartitionList();
}

// ---------------------------------------------------------------------
// Desktop helpers
// ---------------------------------------------------------------------
#ifdef HAS_DESKTOP_TAB
void MainFrame::UncheckDesktopGroup(wxCheckBox *keep) {
    for (auto &o : desktopOpts_) {
        if (o.chk != keep && o.chk->GetValue())
            o.chk->SetValue(false);
    }
}

void MainFrame::SetSelectedDesktop(const char *name) {
    g_free(app_->selected_desktop);
    app_->selected_desktop = g_strdup(name);
}
#endif

// ---------------------------------------------------------------------
// Event handlers
// ---------------------------------------------------------------------
void MainFrame::OnClose(wxCloseEvent &ev) { wxUnusedVar(ev); Destroy(); }

void MainFrame::OnNext(wxCommandEvent &) {
    int cur = notebook_->GetSelection();
    const char *err = ValidateTab(cur);
    if (err) {
        wxMessageDialog dlg(this, wxString::FromUTF8(err),
                            wxString::FromUTF8(get_loc("val_incomplete", app_->current_lang)),
                            wxOK | wxICON_WARNING);
        dlg.ShowModal();
        return;
    }
    notebook_->SetSelection(cur + 1);
}

void MainFrame::OnBack(wxCommandEvent &) {
    int cur = notebook_->GetSelection();
    if (cur > 0) notebook_->SetSelection(cur - 1);
}

void MainFrame::OnPageChanged(wxBookCtrlEvent &) { UpdateNavButtons(); }

void MainFrame::OnLangChanged(wxCommandEvent &) {
    int sel = langChoice_->GetSelection();
    if (sel < 0) return;
    app_->current_lang = sel;
    UpdateUiLanguage();
}

void MainFrame::OnDiskChanged(wxCommandEvent &) {
    HandleDiskSelection();
    UpdateNavButtons();
}

void MainFrame::OnCountryChanged(wxCommandEvent &) { HandleCountrySelection(); }

void MainFrame::OnTzAreaChanged(wxCommandEvent &) { PopulateTzCities(); }

void MainFrame::OnTzCityChanged(wxCommandEvent &) {
    g_free(app_->tz_city);
    app_->tz_city = dupstr(tzCityChoice_->GetStringSelection());
}

void MainFrame::OnKbdLayoutChanged(wxCommandEvent &) {
    app_->kbd_layout_active = kbdLayoutChoice_->GetSelection();
    PopulateKbdVariants();
    app_->kbd_variant_active = 0;
}

void MainFrame::OnKbdVariantChanged(wxCommandEvent &) {
    app_->kbd_variant_active = kbdVariantChoice_->GetSelection();
}

void MainFrame::OnInstallTypeChanged(wxCommandEvent &) {
    int lang = app_->current_lang;
    if (cleanRadio_->GetValue()) {
        app_->install_mode = INSTALL_MODE_ERASE;
        installTypeDesc_->SetLabel(wxString::FromUTF8(get_loc("clean_install_desc", lang)));
    } else {
        app_->install_mode = INSTALL_MODE_DUAL_BOOT;
        installTypeDesc_->SetLabel(wxString::FromUTF8(get_loc("alongside_desc", lang)));
    }
    if (app_->selected_disk) {
        populate_defaults(app_, app_->selected_disk);
        RefreshPartitionList();
    }
}

void MainFrame::OnManualCheck(wxCommandEvent &) {
    bool manual = manualCheck_->GetValue();
    app_->manual_partitioning = manual;
    SetSizerEnabled(installBox_, manual);
    if (manual) {
        app_->install_mode = INSTALL_MODE_MANUAL;
    } else if (app_->selected_disk) {
        app_->install_mode = alongRadio_->GetValue() ? INSTALL_MODE_DUAL_BOOT : INSTALL_MODE_ERASE;
        populate_defaults(app_, app_->selected_disk);
        RefreshPartitionList();
    }
    Layout();
}

void MainFrame::OnAddPartition(wxCommandEvent &) {
    ShowPartitionDialog(NULL);
    UpdateNavButtons();
}

void MainFrame::OnEditPartition(wxCommandEvent &) {
    if (!app_->selected_disk) {
        wxMessageDialog dlg(this, wxT("Please select a disk first!"),
                            wxT("Error"), wxOK | wxICON_ERROR);
        dlg.ShowModal();
        return;
    }
    if (!app_->part_config_list) {
        populate_defaults(app_, app_->selected_disk);
        RefreshPartitionList();
    }

    int sel = mountList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel >= 0) {
        wxString dev = mountList_->GetItemText(sel, 0);
        PartitionConfig *conf = find_config_by_device(app_, dev.utf8_str());
        if (conf) {
            ShowPartitionDialog(conf);
            return;
        }
    }
    if (app_->part_config_list)
        ShowPartitionDialog((PartitionConfig *)app_->part_config_list->data);
}

void MainFrame::OnDeletePartition(wxCommandEvent &) {
    int sel = mountList_->GetNextItem(-1, wxLIST_NEXT_ALL, wxLIST_STATE_SELECTED);
    if (sel >= 0) {
        wxString dev = mountList_->GetItemText(sel, 0);
        PartitionConfig *conf = find_config_by_device(app_, dev.utf8_str());
        if (conf) {
            remove_partition_config(app_, conf);
            RefreshPartitionList();
        }
    }
    UpdateNavButtons();
}

void MainFrame::OnResetPartitions(wxCommandEvent &) {
    core_reset_partitions(app_);
    RefreshPartitionList();
    UpdateNavButtons();
}

void MainFrame::OnGParted(wxCommandEvent &) {
    if (!app_->selected_disk) return;
    wxString cmd = wxT("gparted /dev/") + wxString::FromUTF8(app_->selected_disk);
    wxExecute(cmd);
}

void MainFrame::OnStartInstall(wxCommandEvent &) {
    if (app_->finished) { system("reboot"); return; }
    if (app_->installing) return;
    CollectInstallValues();

    backBtn_->Enable(false);
    nextBtn_->Show(false);
    nextBtn_->Enable(false);
    for (int i = 0; i < TAB_COUNT - 1; i++)
        notebook_->GetPage(i)->Enable(false);
    installBtn_->Enable(false);
    Layout();

    start_installation(app_);
}

void MainFrame::OnReboot(wxCommandEvent &) { system("reboot"); }

#ifdef HAS_DESKTOP_TAB
void MainFrame::OnDesktopLocal(wxCommandEvent &) {
    bool local = desktopLocal_->GetValue();
    desktopSelPanel_->Enable(!local);
    if (local) {
        UncheckDesktopGroup(NULL);
        SetSelectedDesktop("local");
    }
}

void MainFrame::OnDesktopSelected(wxCommandEvent &ev) {
    wxCheckBox *chk = wxDynamicCast(ev.GetEventObject(), wxCheckBox);
    if (!chk || !chk->GetValue()) return;
    UncheckDesktopGroup(chk);
    if (desktopLocal_->GetValue())
        desktopLocal_->SetValue(false);
    desktopSelPanel_->Enable(true);
    for (auto &o : desktopOpts_) {
        if (o.chk == chk) {
            SetSelectedDesktop(o.id.c_str());
            break;
        }
    }
}
#endif

void MainFrame::OnUsernameChar(wxKeyEvent &ev) {
    long selStart = -1, selEnd = -1;
    usernameEntry_->GetSelection(&selStart, &selEnd);
    int key = ev.GetKeyCode();
    if (key >= 0x20 && key < 0x7f) {
        char c = (char)key;
        if (c >= 'A' && c <= 'Z') { c = (char)(c - 'A' + 'a'); key = c; }
        bool ok = false;
        if (c >= 'a' && c <= 'z') ok = true;
        else if (c == '_') ok = true;
        else if (c >= '0' && c <= '9') ok = (selStart > 0);
        else if (c == '-') ok = (selStart > 0);
        if (!ok) return;
        ev.m_keyCode = key;
    }
    ev.Skip();
}

void MainFrame::OnHostnameChar(wxKeyEvent &ev) {
    long selStart = -1, selEnd = -1;
    hostnameEntry_->GetSelection(&selStart, &selEnd);
    int key = ev.GetKeyCode();
    if (key >= 0x20 && key < 0x7f) {
        char c = (char)key;
        if (c >= 'A' && c <= 'Z') { c = (char)(c - 'A' + 'a'); key = c; }
        bool ok = false;
        if (c >= 'a' && c <= 'z') ok = true;
        else if (c >= '0' && c <= '9') ok = true;
        else if (c == '_') ok = true;
        else if (c == '-') ok = (selStart > 0);
        else if (c == '.') ok = (selStart > 0);
        if (!ok) return;
        ev.m_keyCode = key;
    }
    ev.Skip();
}

void MainFrame::OnDrainTimer(wxTimerEvent &) {
    core_flush_capture(app_);
    CoreLogEntry *e = core_drain_log();
    while (e) {
        if (e->finished) {
            OnInstallFinished();
        } else {
            if (e->message && e->message[0]) {
                console_->AppendText(wxString::FromUTF8(e->message));
                console_->ShowPosition(console_->GetLastPosition());
            }
            if (e->fraction >= 0)
                gauge_->SetValue((int)(e->fraction * 100.0));
        }
        CoreLogEntry *n = e->next;
        g_free(e->message);
        g_free(e);
        e = n;
    }
}

void MainFrame::OnPulseTimer(wxTimerEvent &) {
    if (app_->pulsing && !app_->finished) {
        pulseValue_ = (pulseValue_ + 1) % 100;
        gauge_->SetValue(pulseValue_);
    }
}
