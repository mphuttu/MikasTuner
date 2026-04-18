#pragma once
#include <afxwin.h>
class CTuneBar :
    public CStatic
{
public:
	CTuneBar();
	~CTuneBar();
    void OnPaint();
	

	void SetCents(double cents) { m_cents = cents; Invalidate(); } // neulan paikka senteissä
	DECLARE_MESSAGE_MAP()

private:
	double m_cents = 0.0; // neulan paikka senteissä
};

