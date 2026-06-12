// ServerDialog.cpp : implementation file
//

#include "pch.h"
#include "CServerTabPanel.h"
#include "config_selection.hpp"
#include "resource.h"
#include "audio_manager.hpp"
#include "network_manager.hpp"
#include "AudioShareServer.h"

// CServerTabPanel dialog

IMPLEMENT_DYNAMIC(CServerTabPanel, CTabPanel)

CServerTabPanel::CServerTabPanel(CWnd* pParent)
    : CTabPanel(IDD_SERVER, IDS_SERVER, pParent)
    , m_bStarted(false)
{
}

CServerTabPanel::~CServerTabPanel()
{
}

void CServerTabPanel::SwitchServer()
{
    m_buttonServer.PostMessageW(BM_CLICK);
}

bool CServerTabPanel::IsRunning()
{
    return m_bStarted;
}

void CServerTabPanel::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_COMBO_HOST, m_comboBoxHost);
    DDX_Control(pDX, IDC_EDIT_PORT, m_editPort);
    DDX_Control(pDX, IDC_COMBO_AUDIO_ENDPOINT, m_comboBoxAudioEndpoint);
    DDX_Control(pDX, IDC_BUTTON_SERVER, m_buttonServer);
    DDX_Control(pDX, IDC_BUTTON_RESET, m_buttonReset);
    DDX_Control(pDX, IDC_COMBO_ENCODING, m_comboEncoding);
    DDX_Control(pDX, IDC_BUTTON_SOUND_PANEL, m_buttonSoundPanel);
}


BEGIN_MESSAGE_MAP(CServerTabPanel, CTabPanel)
    ON_BN_CLICKED(IDC_BUTTON_SERVER, &CServerTabPanel::OnBnClickedStartServer)
    ON_BN_CLICKED(IDC_BUTTON_RESET, &CServerTabPanel::OnBnClickedButtonReset)
    ON_BN_CLICKED(IDC_BUTTON_SOUND_PANEL, &CServerTabPanel::OnBnClickedButtonSoundPanel)
END_MESSAGE_MAP()


// CServerTabPanel message handlers

BOOL CServerTabPanel::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    // TODO:  Add extra initialization here
    m_editPort.SetLimitText(5);

    // Create the managers before populating the controls: OnBnClickedButtonReset()
    // enumerates audio endpoints through m_audio_manager, so it must exist first.
    m_audio_manager = std::make_shared<audio_manager>();
    m_network_manager = std::make_shared<network_manager>(m_audio_manager);

    // init controls data
    this->OnBnClickedButtonReset();

    auto nWhenAppStart = theApp.GetProfileIntW(L"App Settings", L"WhenAppStart", 0);
    if (nWhenAppStart == 1 || nWhenAppStart == 2 && theApp.GetProfileIntW(L"App", L"Running", false)) {
        m_buttonServer.PostMessageW(BM_CLICK);
    }

    return TRUE;  // return TRUE unless you set the focus to a control
    // EXCEPTION: OCX Property Pages should return FALSE
}


void CServerTabPanel::OnBnClickedButtonReset()
{
    // host list
    {
        m_comboBoxHost.ResetContent();
        auto address_list = network_manager::get_address_list();
        for (auto address : address_list) {
            auto nIndex = m_comboBoxHost.AddString(mbs_to_wchars(address).c_str());
        }
        auto default_address = network_manager::get_default_address();
        auto configHost = theApp.GetProfileStringW(L"Network", L"host", default_address.empty() ? nullptr : mbs_to_wchars(default_address).c_str());
        m_comboBoxHost.SetWindowTextW(configHost);
    }

    // port
    {
        m_editPort.SetWindowTextW(theApp.GetProfileStringW(L"Network", L"port", L"65530"));
    }

    CString defaultString;
    (void)defaultString.LoadStringW(IDS_DEFAULT);

    // audio endpoint list
    {
        for (int nIndex = m_comboBoxAudioEndpoint.GetCount() - 1; nIndex >= 0; --nIndex) {
            free(m_comboBoxAudioEndpoint.GetItemDataPtr(nIndex));
        }
        m_comboBoxAudioEndpoint.ResetContent();

        // keys aligned 1:1 with the combo items, used to restore the saved selection
        std::vector<std::wstring> endpoint_keys;

        auto nDefaultIndex = m_comboBoxAudioEndpoint.AddString(defaultString);
        m_comboBoxAudioEndpoint.SetItemDataPtr(nDefaultIndex, _wcsdup(L"default"));
        endpoint_keys.push_back(L"default");
        
        // m_audio_manager is created before this runs; guard anyway so a missing
        // audio subsystem degrades to "only default" instead of crashing.
        if (m_audio_manager) {
            audio_manager::endpoint_list_t endpoint_list = m_audio_manager->get_endpoint_list();
            for (auto&& [id, name] : endpoint_list) {
                int nIndex = m_comboBoxAudioEndpoint.AddString(mbs_to_wchars(name).c_str());
                std::wstring endpoint_id = mbs_to_wchars(id);
                m_comboBoxAudioEndpoint.SetItemDataPtr(nIndex, _wcsdup(endpoint_id.c_str()));
                endpoint_keys.push_back(endpoint_id);
            }
        }

        // Restore the saved endpoint. If it is gone (unplugged, list changed,
        // first run) fall back to the default item. Resolving the selection must
        // not write to the registry: the saved endpoint id is kept so the choice
        // returns when the device comes back, and the encoding setting is left
        // completely untouched.
        auto configEndpoint = theApp.GetProfileStringW(L"Capture", L"endpoint", L"default");
        int nSel = config_selection::resolve_selected_index<std::wstring>(endpoint_keys, configEndpoint.GetString());
        m_comboBoxAudioEndpoint.SetCurSel(nSel);
    }

    // encoding list
    {
        m_comboEncoding.ResetContent();
        using encoding_t = audio_manager::encoding_t;
        std::vector<std::pair<encoding_t, std::wstring>> array = {
            { encoding_t::encoding_default, defaultString.GetString() },
            { encoding_t::encoding_f32, L"32 bit floating-point PCM" },
            { encoding_t::encoding_s8, L"8 bit integer PCM" },
            { encoding_t::encoding_s16, L"16 bit integer PCM" },
            { encoding_t::encoding_s24, L"24 bit integer PCM" },
            { encoding_t::encoding_s32, L"32 bit integer PCM" },
        };
        std::vector<int> encoding_keys;
        for (auto&& [encoding, name] : array) {
            auto nIndex = m_comboEncoding.AddString(name.c_str());
            m_comboEncoding.SetItemData(nIndex, (int)encoding);
            encoding_keys.push_back((int)encoding);
        }
        
        // Restore the saved encoding, falling back to the default item when the
        // stored value is unknown. Like the endpoint above this performs no
        // registry write, so it can never clobber the saved endpoint -- the
        // original defect wrote the encoding fallback into the "endpoint" key.
        auto configEncoding = theApp.GetProfileIntW(L"Capture", L"encoding", (int)encoding_t::encoding_default);
        int nSel = config_selection::resolve_selected_index<int>(encoding_keys, (int)configEncoding);
        m_comboEncoding.SetCurSel(nSel);
    }
}

void CServerTabPanel::OnBnClickedStartServer()
{
    if (!m_network_manager) {
        AfxMessageBox(L"network_manager is nullptr", MB_OK | MB_ICONSTOP);
        EndDialog(-1);
        return;
    }

    if (!IsRunning()) {
        if (m_comboBoxAudioEndpoint.GetCount() == 1) {
            AfxMessageBox(IDS_NO_ENDPOINT, MB_OK | MB_ICONSTOP);
            return;
        }

        if (m_comboBoxAudioEndpoint.GetCurSel() == CB_ERR) {
            AfxMessageBox(IDS_NO_SELECTED_ENDPOINT, MB_OK | MB_ICONSTOP);
            return;
        }

        // start
        EnableInputControls(false);
        m_buttonServer.EnableWindow(false);

        CString host_str, port_str;
        m_comboBoxHost.GetWindowTextW(host_str);
        m_editPort.GetWindowTextW(port_str);
        std::string host = wchars_to_mbs(host_str.GetString());
        std::uint16_t port = std::stoi(wchars_to_mbs(port_str.GetString()));
        audio_manager::capture_config config;
        config.endpoint_id = wchars_to_mbs((LPCWSTR)m_comboBoxAudioEndpoint.GetItemDataPtr(m_comboBoxAudioEndpoint.GetCurSel()));
        config.encoding = (audio_manager::encoding_t)m_comboEncoding.GetItemData(m_comboEncoding.GetCurSel());
        try {
            m_network_manager->start_server(host, port, config);
        }
        catch (std::exception& e) {
            AfxMessageBox(CString(e.what()), MB_OK | MB_ICONSTOP);
            EnableInputControls(true);
            m_buttonServer.EnableWindow(true);
            CString s;
            (void)s.LoadStringW(IDS_START_SERVER);
            m_buttonServer.SetWindowTextW(s);
            m_buttonServer.SetFocus();
            return;
        }

        m_buttonServer.EnableWindow(true);
        CString s;
        (void)s.LoadStringW(IDS_STOP_SERVER);
        m_buttonServer.SetWindowTextW(s);
        m_buttonServer.SetFocus();
        theApp.WriteProfileStringW(L"Network", L"host", host_str);
        theApp.WriteProfileStringW(L"Network", L"port", port_str);
        theApp.WriteProfileStringW(L"Capture", L"endpoint", mbs_to_wchars(config.endpoint_id).c_str());
        theApp.WriteProfileInt(L"Capture", L"encoding", (int)config.encoding);
        theApp.WriteProfileInt(L"App", L"Running", true);
        m_bStarted = true;
    }
    else {
        // stop
        m_buttonServer.EnableWindow(false);
        m_network_manager->stop_server();

        EnableInputControls(true);
        m_buttonServer.EnableWindow(true);
        CString s;
        (void)s.LoadStringW(IDS_START_SERVER);
        m_buttonServer.SetWindowTextW(s);
        m_buttonServer.SetFocus();
        theApp.WriteProfileInt(L"App", L"Running", false);
        m_bStarted = false;
    }
}

void CServerTabPanel::EnableInputControls(bool bEnable)
{
    m_comboBoxHost.EnableWindow(bEnable);
    m_editPort.EnableWindow(bEnable);
    m_comboBoxAudioEndpoint.EnableWindow(bEnable);
    m_buttonReset.EnableWindow(bEnable);
    m_comboEncoding.EnableWindow(bEnable);
    m_buttonSoundPanel.EnableWindow(bEnable);
}


void CServerTabPanel::OnBnClickedButtonSoundPanel()
{
    STARTUPINFO si{};
    PROCESS_INFORMATION pi{};

    LPWSTR lpCommandLine = _wcsdup(L"control.exe /name Microsoft.Sound");
    auto hr = CreateProcessW(nullptr, lpCommandLine, nullptr, nullptr, false, 0, nullptr, nullptr, &si, &pi);
    
    WaitForSingleObject(pi.hProcess, INFINITE);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    free(lpCommandLine);
}
