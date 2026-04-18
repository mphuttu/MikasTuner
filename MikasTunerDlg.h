#pragma once

#include <mmsystem.h>
#include "CPitchDetector.h"
#include "CTuneBar.h"

#pragma comment(lib, "winmm.lib")

// CMikasTunerDlg dialog
class CMikasTunerDlg : public CDialogEx
{
public:
    CMikasTunerDlg(CWnd* pParent = nullptr);
    virtual ~CMikasTunerDlg();

    enum { IDD = IDD_MIKASTUNER_DIALOG };

protected:
    virtual void DoDataExchange(CDataExchange* pDX);
    virtual BOOL OnInitDialog();
    afx_msg void OnPaint();
    afx_msg HCURSOR OnQueryDragIcon();
    afx_msg void OnDestroy();
    afx_msg void OnBnClickedButtonStart();
    afx_msg void OnBnClickedButtonStop();
    afx_msg LRESULT OnUpdateFrequency(WPARAM wParam, LPARAM lParam);
    DECLARE_MESSAGE_MAP()

private:
    // Audio recording
    HWAVEIN m_hWaveIn;
    WAVEHDR m_waveHeader;
    std::vector<short> m_audioBuffer;
    bool m_isRecording;

    // Pitch detection
    CPitchDetector* m_detector;

    // UI update data (thread-safe)
    double m_displayFrequency;
    double m_displayCents;
    CString m_displayStringName;
    bool m_displayValid;

    // Methods
    bool StartAudio();
    void StopAudio();
    void OnAudioData(WAVEHDR* hdr);
    void UpdateTunerDisplay();
    void FillMicrophoneList();
    UINT GetSelectedMicrophoneIndex();
    void ResetAllBars();
    void UpdateStringBar(const CString& stringName, double cents);

    // Static callback for audio
    static void CALLBACK WaveInProc(HWAVEIN hwi, UINT uMsg, DWORD_PTR dwInstance,
                                     DWORD_PTR dwParam1, DWORD_PTR dwParam2);

    // UI elements
    HICON m_hIcon;
    CStatic m_staticFrequency;
    CStatic m_staticStringName;
    CStatic m_staticCents;
    CStatic m_staticStatus;
    CProgressCtrl m_progressTuning;

    // DoDataExchange-kontrollit:
    CTuneBar m_barELow;
    CTuneBar m_barA;
    CTuneBar m_barD;
    CTuneBar m_barG;
    CTuneBar m_barH;
    CTuneBar m_barEHigh;
};
