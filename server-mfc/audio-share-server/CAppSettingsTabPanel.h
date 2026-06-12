#pragma once

#include "CTabPanel.h"

#include <atomic>
#include <memory>
#include <thread>

// CAppSettingsTabPanel dialog

class CAppSettingsTabPanel : public CTabPanel
{
	DECLARE_DYNAMIC(CAppSettingsTabPanel)
public:
	CAppSettingsTabPanel(CWnd* pParent);   // standard constructor
	virtual ~CAppSettingsTabPanel();

// Dialog Data
#ifdef AFX_DESIGN_TIME
	enum { IDD = IDD_APP_SETTINGS };
#endif

protected:
	virtual void DoDataExchange(CDataExchange* pDX);    // DDX/DDV support

	DECLARE_MESSAGE_MAP()
public:
	virtual BOOL OnInitDialog();
	afx_msg void OnBnClickedCheckAutoRun();
	afx_msg void OnBnClickedButtonReppairFirewall();
	void SetAutoRun(bool bEnable);
	afx_msg void OnBnClickedButtonHide();
	afx_msg void OnBnClickedCheckAutoUpdate();
	afx_msg void OnBnClickedWhenAppStart(UINT nID);
	afx_msg void OnBnClickedWhenCloseButton(UINT nID);
	afx_msg void OnBnClickedButtonUpdate();
	void CheckForUpdate(bool bPromptError);
	afx_msg void OnDestroy();

public:
	LPCWSTR m_lpszSection;
	CButton m_buttonRepairFirewall;
	// behavior when app start
	// 0 - do nothing
	// 1 - start server
	// 2 - keep state
	int m_nWhenAppStart;
	// behavior when click close button
	// 0 - exit
	// 1 - minimize
	int m_nWhenClose;
	// auto run app
	BOOL m_bAutoRun;
	// auto check for update
	BOOL m_bAutoUpdate;
	afx_msg void OnTimer(UINT_PTR nIDEvent);
	CComboBox m_comboLanguage;
	afx_msg void OnCbnSelchangeComboLanguage();

	// Background update-check worker. It is cancelled and joined in OnDestroy so
	// it can never touch a destroyed window or keep running into process teardown.
	std::jthread m_updateThread;
	// true while a check is running, so the auto-update timer and the manual
	// button don't launch overlapping checks. Held via shared_ptr so the worker
	// can clear it without holding a reference to this (possibly destroyed) panel.
	std::shared_ptr<std::atomic<bool>> m_pUpdating = std::make_shared<std::atomic<bool>>(false);
};
