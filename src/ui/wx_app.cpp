#include <wx/wx.h>
#include "wx_frame.h"
#include "neko_installer.h"
#include "lang.h"

class InstallerApp : public wxApp {
public:
    virtual bool OnInit() override {
        wxInitAllImageHandlers();
        lang_init();

        AppData *app = g_new0(AppData, 1);
        app->current_lang = 0;

        for (int i = 1; i < argc; i++) {
            if (wxStrcmp(argv[i], wxT("--debug-ui")) == 0) {
                app->debug_mode = TRUE;
                wxPrintf(wxT("[DEBUG] UI-only mode enabled - commands will be logged only\n"));
            }
        }

        // Hardware detection (EFI + firmware bits)
        core_detect_hardware(app);

        MainFrame *frame = new MainFrame(app);
        frame->Show(true);

        return true;
    }
};

wxIMPLEMENT_APP(InstallerApp);
