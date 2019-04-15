#include "../Image3dAPI/ComSupport.hpp"
#include "../Image3dAPI/IImage3d.h"
#include "../Image3dAPI/RegistryCheck.hpp"
#include <iostream>
#include <sddl.h>


/** RAII class for temporarily impersonating low-integrity level for the current thread.
    Intended to be used together with CLSCTX_ENABLE_CLOAKING when creating COM objects.
    Based on "Designing Applications to Run at a Low Integrity Level" https://msdn.microsoft.com/en-us/library/bb625960.aspx */
struct LowIntegrity {
    LowIntegrity()
    {
        HANDLE cur_token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_DUPLICATE | TOKEN_ADJUST_DEFAULT | TOKEN_QUERY | TOKEN_ASSIGN_PRIMARY, &cur_token))
            abort();

        if (!DuplicateTokenEx(cur_token, 0, NULL, SecurityImpersonation, TokenPrimary, &m_token))
            abort();

        CloseHandle(cur_token);

        PSID li_sid = nullptr;
        if (!ConvertStringSidToSid(L"S-1-16-4096", &li_sid)) // low integrity SID
            abort();

        // reduce process integrity level
        TOKEN_MANDATORY_LABEL TIL = {};
        TIL.Label.Attributes = SE_GROUP_INTEGRITY;
        TIL.Label.Sid = li_sid;
        if (!SetTokenInformation(m_token, TokenIntegrityLevel, &TIL, sizeof(TOKEN_MANDATORY_LABEL) + GetLengthSid(li_sid)))
            abort();

        if (!ImpersonateLoggedOnUser(m_token)) // change current thread integrity
            abort();

        LocalFree(li_sid);
        li_sid = nullptr;
    }

    ~LowIntegrity()
    {
        if (!RevertToSelf())
            abort();

        CloseHandle(m_token);
        m_token = nullptr;
    }

private:
    HANDLE m_token = nullptr;
};


void ParseSource (IImage3dSource & source) {
    Cart3dGeom geom = {};
    CHECK(source.GetBoundingBox(&geom));

    unsigned int frame_count = 0;
    CHECK(source.GetFrameCount(&frame_count));

    CComSafeArray<unsigned int> color_map;
    {
        SAFEARRAY * tmp = nullptr;
        CHECK(source.GetColorMap(&tmp));
        color_map.Attach(tmp);
        tmp = nullptr;
    }

    for (unsigned int frame = 0; frame < frame_count; ++frame) {
        unsigned short max_res[] = {64, 64, 64};

        // retrieve frame data
        Image3d data;
        CHECK(source.GetFrame(frame, geom, max_res, &data));
    }
}


int wmain (int argc, wchar_t *argv[]) {
    if (argc < 3) {
        std::wcout << L"Usage:\n";
        std::wcout << L"SandboxTest.exe <loader-progid> <filename>" << std::endl;
        return -1;
    }

    CComBSTR progid = argv[1];  // e.g. "DummyLoader.Image3dFileLoader"
    CComBSTR filename = argv[2];

    ComInitialize com(COINIT_MULTITHREADED);

    CLSID clsid = {};
    CHECK(CLSIDFromProgID(progid, &clsid));

    auto list = SupportedManufacturerModels::ReadList(clsid);

    // verify that loader library is compatible
    CHECK(CheckImage3dAPIVersion(clsid));

    // create loader in a separate "low integrity" dllhost.exe process
    CComPtr<IImage3dFileLoader> loader;
    {
        LowIntegrity low_integrity;
        CHECK(loader.CoCreateInstance(clsid, nullptr, CLSCTX_LOCAL_SERVER | CLSCTX_ENABLE_CLOAKING));
    }

    {
        // load file
        Image3dError err_type = {};
        CComBSTR err_msg;
        CHECK(loader->LoadFile(filename, &err_type, &err_msg));
    }

    CComPtr<IImage3dSource> source;
    CHECK(loader->GetImageSource(&source));

    ProbeInfo probe;
    CHECK(source->GetProbeInfo(&probe));

    EcgSeries ecg;
    CHECK(source->GetECG(&ecg));

    ParseSource(*source);

    return 0;
}