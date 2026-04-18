#include "pch.h"
#include "framework.h"
#include "MikasTuner.h"
#include "MikasTunerDlg.h"
#include "afxdialogex.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

// Custom message for UI updates from audio thread
#define WM_UPDATE_FREQUENCY (WM_USER + 1)

// Audio settings
constexpr int SAMPLE_RATE = 44100;
constexpr int BUFFER_SIZE = 4096;  // Samples per buffer

CMikasTunerDlg::CMikasTunerDlg(CWnd* pParent /*=nullptr*/)
    : CDialogEx(IDD_MIKASTUNER_DIALOG, pParent)
    , m_hWaveIn(nullptr)
    , m_waveHeader{}
    , m_isRecording(false)
    , m_detector(nullptr)
    , m_displayFrequency(0.0)
    , m_displayCents(0.0)
    , m_displayValid(false)
{
    m_hIcon = AfxGetApp()->LoadIcon(IDR_MAINFRAME);
    m_audioBuffer.resize(BUFFER_SIZE);
}

CMikasTunerDlg::~CMikasTunerDlg()
{
    StopAudio();
    delete m_detector;
    m_detector = nullptr;
}

void CMikasTunerDlg::DoDataExchange(CDataExchange* pDX)
{
    CDialogEx::DoDataExchange(pDX);
    DDX_Control(pDX, IDC_FREQTEXT, m_staticFrequency);
    DDX_Control(pDX, IDC_STATIC_STRING, m_staticStringName);
    DDX_Control(pDX, IDC_STATIC_CENTS, m_staticCents);
    DDX_Control(pDX, IDC_STATIC_STATUS, m_staticStatus);
    DDX_Control(pDX, IDC_PROGRESS_TUNING, m_progressTuning);

    // CTuneBar kontrollit kullekin kielelle
    DDX_Control(pDX, IDC_BAR_E_LOW, m_barELow);
    DDX_Control(pDX, IDC_BAR_A, m_barA);
    DDX_Control(pDX, IDC_BAR_D, m_barD);
    DDX_Control(pDX, IDC_BAR_G, m_barG);
    DDX_Control(pDX, IDC_BAR_H, m_barH);
    DDX_Control(pDX, IDC_BAR_E_HIGH, m_barEHigh);
}

BEGIN_MESSAGE_MAP(CMikasTunerDlg, CDialogEx)
    ON_WM_PAINT()
    ON_WM_QUERYDRAGICON()
    ON_WM_DESTROY()
    ON_BN_CLICKED(IDC_START, &CMikasTunerDlg::OnBnClickedButtonStart)
    ON_BN_CLICKED(IDC_STOP, &CMikasTunerDlg::OnBnClickedButtonStop)
    ON_MESSAGE(WM_UPDATE_FREQUENCY, &CMikasTunerDlg::OnUpdateFrequency)
END_MESSAGE_MAP()

BOOL CMikasTunerDlg::OnInitDialog()
{
    CDialogEx::OnInitDialog();

    SetIcon(m_hIcon, TRUE);
    SetIcon(m_hIcon, FALSE);

    // Initialize pitch detector
    m_detector = new CPitchDetector(SAMPLE_RATE);

    // Configure detector for guitar tuning
    m_detector->SetNoiseThreshold(400.0);   // Adjust based on environment
    m_detector->SetMinConfidence(0.35);      // Require good signal quality
    m_detector->SetSmoothingFactor(0.25);    // Smooth frequency changes

    // Fill microphone list
    FillMicrophoneList();

    // Initialize progress bar
    m_progressTuning.SetRange(-50, 50);
    m_progressTuning.SetPos(0);

    // Set initial UI state
    m_staticFrequency.SetWindowText(_T("---"));
    m_staticStringName.SetWindowText(_T(""));
    m_staticCents.SetWindowText(_T(""));
    m_staticStatus.SetWindowText(_T("Push 'Start' to begin"));

    // Disable stop button initially
    GetDlgItem(IDC_STOP)->EnableWindow(FALSE);

    return TRUE;
}

void CMikasTunerDlg::OnPaint()
{
    if (IsIconic())
    {
        CPaintDC dc(this);
        SendMessage(WM_ICONERASEBKGND, reinterpret_cast<WPARAM>(dc.GetSafeHdc()), 0);
        int cxIcon = GetSystemMetrics(SM_CXICON);
        int cyIcon = GetSystemMetrics(SM_CYICON);
        CRect rect;
        GetClientRect(&rect);
        int x = (rect.Width() - cxIcon + 1) / 2;
        int y = (rect.Height() - cyIcon + 1) / 2;
        dc.DrawIcon(x, y, m_hIcon);
    }
    else
    {
        CDialogEx::OnPaint();
    }
}

HCURSOR CMikasTunerDlg::OnQueryDragIcon()
{
    return static_cast<HCURSOR>(m_hIcon);
}

void CMikasTunerDlg::OnDestroy()
{
    StopAudio();
    CDialogEx::OnDestroy();
}

void CMikasTunerDlg::OnBnClickedButtonStart()
{
    if (StartAudio())
    {
        GetDlgItem(IDC_START)->EnableWindow(FALSE);
        GetDlgItem(IDC_STOP)->EnableWindow(TRUE);
        m_staticStatus.SetWindowText(_T("Let's listen... Play a string"));

        // Reset detector when starting
        if (m_detector)
        {
            m_detector->Reset();
        }
    }
    else
    {
        AfxMessageBox(_T("Starting the sound recording failed!\nCheck the microphone."), MB_ICONERROR);
    }
}

void CMikasTunerDlg::OnBnClickedButtonStop()
{
    StopAudio();
    GetDlgItem(IDC_START)->EnableWindow(TRUE);
    GetDlgItem(IDC_STOP)->EnableWindow(FALSE);
    m_staticStatus.SetWindowText(_T("Sound recording stopped"));
    m_staticFrequency.SetWindowText(_T("---"));
    m_staticStringName.SetWindowText(_T(""));
    m_staticCents.SetWindowText(_T(""));
    m_progressTuning.SetPos(0);
}

bool CMikasTunerDlg::StartAudio()
{
    if (m_isRecording)
        return true;

    // Set up audio format (16-bit mono)
    WAVEFORMATEX wfx = {};
    wfx.wFormatTag = WAVE_FORMAT_PCM;
    wfx.nChannels = 1;
    wfx.nSamplesPerSec = SAMPLE_RATE;
    wfx.wBitsPerSample = 16;
    wfx.nBlockAlign = wfx.nChannels * wfx.wBitsPerSample / 8;
    wfx.nAvgBytesPerSec = wfx.nSamplesPerSec * wfx.nBlockAlign;

    // Open audio input device using selected microphone
    UINT deviceId = GetSelectedMicrophoneIndex();
    MMRESULT result = waveInOpen(&m_hWaveIn, deviceId, &wfx,
                                  reinterpret_cast<DWORD_PTR>(WaveInProc),
                                  reinterpret_cast<DWORD_PTR>(this),
                                  CALLBACK_FUNCTION);
    if (result != MMSYSERR_NOERROR)
    {
        TRACE(_T("waveInOpen failed: %d\n"), result);
        return false;
    }

    // Prepare buffer
    m_waveHeader = {};
    m_waveHeader.lpData = reinterpret_cast<LPSTR>(m_audioBuffer.data());
    m_waveHeader.dwBufferLength = BUFFER_SIZE * sizeof(short);

    result = waveInPrepareHeader(m_hWaveIn, &m_waveHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
        return false;
    }

    result = waveInAddBuffer(m_hWaveIn, &m_waveHeader, sizeof(WAVEHDR));
    if (result != MMSYSERR_NOERROR)
    {
        waveInUnprepareHeader(m_hWaveIn, &m_waveHeader, sizeof(WAVEHDR));
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
        return false;
    }

    result = waveInStart(m_hWaveIn);
    if (result != MMSYSERR_NOERROR)
    {
        waveInUnprepareHeader(m_hWaveIn, &m_waveHeader, sizeof(WAVEHDR));
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
        return false;
    }

    m_isRecording = true;
    return true;
}

void CMikasTunerDlg::StopAudio()
{
    if (!m_isRecording)
        return;

    m_isRecording = false;

    if (m_hWaveIn)
    {
        waveInStop(m_hWaveIn);
        waveInReset(m_hWaveIn);
        waveInUnprepareHeader(m_hWaveIn, &m_waveHeader, sizeof(WAVEHDR));
        waveInClose(m_hWaveIn);
        m_hWaveIn = nullptr;
    }
}

void CMikasTunerDlg::FillMicrophoneList()
{
    CComboBox* combo = (CComboBox*)GetDlgItem(IDC_MICLIST);
    if (!combo)
        return;

    combo->ResetContent();

    UINT num = waveInGetNumDevs();
    for (UINT i = 0; i < num; ++i)
    {
        WAVEINCAPS caps = {};
        if (waveInGetDevCaps(i, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        {
            combo->AddString(caps.szPname);
        }
    }

    if (num > 0)
        combo->SetCurSel(0);
}

UINT CMikasTunerDlg::GetSelectedMicrophoneIndex()
{
    CComboBox* combo = (CComboBox*)GetDlgItem(IDC_MICLIST);
    if (!combo)
        return WAVE_MAPPER;

    int sel = combo->GetCurSel();
    if (sel == CB_ERR)
        return WAVE_MAPPER;

    return static_cast<UINT>(sel);
}

// Static callback
void CALLBACK CMikasTunerDlg::WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
                                          DWORD_PTR dwParam1, DWORD_PTR dwParam2)
{
    CMikasTunerDlg* pThis = reinterpret_cast<CMikasTunerDlg*>(dwInstance);
    if (pThis && pThis->m_isRecording)
    {
        if (uMsg == WIM_DATA)
        {
            pThis->OnAudioData(reinterpret_cast<WAVEHDR*>(dwParam1));
        }
    }
}

void CMikasTunerDlg::OnAudioData(WAVEHDR* hdr)
{
    if (!m_detector || !m_isRecording)
        return;

    int count = hdr->dwBytesRecorded / sizeof(short);

    // Process audio with pitch detector
    m_detector->ProcessBuffer(reinterpret_cast<short*>(hdr->lpData), count);

    // Prepare data for UI update (thread-safe copy)
    if (m_detector->IsValidSignal())
    {
        m_displayFrequency = m_detector->GetFrequency();
        m_displayCents = m_detector->GetCentsOff();

        const auto* nearestString = m_detector->GetNearestString();
        if (nearestString)
        {
            m_displayStringName = nearestString->name;
        }
        m_displayValid = true;
    }
    else
    {
        m_displayValid = false;
    }

    // Post message to update UI on main thread
    PostMessage(WM_UPDATE_FREQUENCY, 0, 0);

    // Re-add buffer for next recording
    if (m_isRecording && m_hWaveIn)
    {
        waveInAddBuffer(m_hWaveIn, hdr, sizeof(WAVEHDR));
    }

    TRACE(_T("Audio callback: %d samples, freq=%.1f Hz, valid=%d\n"), 
          count, m_displayFrequency, m_displayValid);
}

LRESULT CMikasTunerDlg::OnUpdateFrequency(WPARAM wParam, LPARAM lParam)
{
    UpdateTunerDisplay();
    return 0;
}

void CMikasTunerDlg::UpdateTunerDisplay()
{
    // Reset all bars first
    ResetAllBars();

    if (!m_displayValid)
    {
        m_staticFrequency.SetWindowText(_T("---"));
        m_staticStringName.SetWindowText(_T(""));
        m_staticCents.SetWindowText(_T(""));
        m_staticStatus.SetWindowText(_T("Listening... Play a string"));
        m_progressTuning.SetPos(0);
        return;
    }

    // Display frequency
    CString strFreq;
    strFreq.Format(_T("%.1f Hz"), m_displayFrequency);
    m_staticFrequency.SetWindowText(strFreq);

    // Display detected string
    m_staticStringName.SetWindowText(m_displayStringName);

    // Display cents offset and tuning status
    CString strCents;
    CString strStatus;
    int centsInt = static_cast<int>(m_displayCents);

    if (m_displayCents > -3.0 && m_displayCents < 3.0)
    {
        // In tune!
        strCents = _T("OK - In tune!");
        strStatus = _T("The string is in tune!");
    }
    else if (m_displayCents < 0)
    {
        // Too low
        strCents.Format(_T("FLAT: %.0f cents low"), -m_displayCents);
        strStatus = _T("Tighten the string!");
    }
    else
    {
        // Too high
        strCents.Format(_T("SHARP: +%.0f cents high"), m_displayCents);
        strStatus = _T("Loosen the string!");
    }

    m_staticCents.SetWindowText(strCents);
    m_staticStatus.SetWindowText(strStatus);

    // Update progress bar (clamp to -50..+50)
    int progressPos = centsInt;
    if (progressPos < -50) progressPos = -50;
    if (progressPos > 50) progressPos = 50;
    m_progressTuning.SetPos(progressPos);

    // Update the correct CTuneBar based on detected string
    UpdateStringBar(m_displayStringName, m_displayCents);
}

void CMikasTunerDlg::ResetAllBars()
{
    m_barELow.SetCents(0.0);
    m_barA.SetCents(0.0);
    m_barD.SetCents(0.0);
    m_barG.SetCents(0.0);
    m_barH.SetCents(0.0);
    m_barEHigh.SetCents(0.0);
}

void CMikasTunerDlg::UpdateStringBar(const CString& stringName, double cents)
{
    // Update only the detected string's bar
    if (stringName == _T("E (matala)"))
    {
        m_barELow.SetCents(cents);
    }
    else if (stringName == _T("A"))
    {
        m_barA.SetCents(cents);
    }
    else if (stringName == _T("D"))
    {
        m_barD.SetCents(cents);
    }
    else if (stringName == _T("G"))
    {
        m_barG.SetCents(cents);
    }
    else if (stringName == _T("B (H)"))
    {
        m_barH.SetCents(cents);
    }
    else if (stringName == _T("E (korkea)"))
    {
        m_barEHigh.SetCents(cents);
    }
}
