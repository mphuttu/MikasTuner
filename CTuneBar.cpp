#include "pch.h"
#include "CTuneBar.h"

#ifdef max
#undef max
#endif
#ifdef min
#undef min
#endif

#include <algorithm>

BEGIN_MESSAGE_MAP(CTuneBar, CStatic)
	ON_WM_PAINT()
END_MESSAGE_MAP()

CTuneBar::CTuneBar()
{
}

CTuneBar::~CTuneBar()
{
}

void CTuneBar::OnPaint()
{
    // TODO: Add your message handler code here
    // Do not call CStatic::OnPaint() for painting messages
	CPaintDC dc(this); // device context for painting
    CRect rc;
	GetClientRect(&rc);

	dc.FillSolidRect(rc, RGB(30, 30, 30));

	// vihreä keskialue
	int mid = (rc.left + rc.right) / 2;
	int tol = (rc.Width() / 10); // 10% tol	
	CRect rcGreen(mid - tol, rc.top +5 , mid + tol, rc.bottom - 5);
	dc.FillSolidRect(rcGreen, RGB(0, 150, 0));

	// neulan paikka senteissä
	double cents = m_cents; // -50 ... +50
	double maxCents = 50.0; 
	double t = std::max(-maxCents, std::min(cents, maxCents)) / (2 * maxCents) + 0.5; // -1 ... +1
	int x = rc.left + static_cast<int>(t * rc.Width());

	CPen pen(PS_SOLID, 2, RGB(255, 255, 0));
	CPen* oldPen = dc.SelectObject(&pen);
	dc.MoveTo(x, rc.top);
	dc.LineTo(x, rc.bottom);
	dc.SelectObject(oldPen);

	CPen pen1(PS_SOLID, 1, RGB(255, 0, 0));
	CPen* oldPen1 = dc.SelectObject(&pen);
	// Asteikkoviivat
	for (int i = -50; i <= 50; i += 10)
	{
		double t = (i + 50) / 100.0; // -50 ... +50 -> 0 ... 1
		int x = rc.left + static_cast<int>(t * rc.Width());
		dc.MoveTo(x, rc.top + 2);
		dc.LineTo(x, rc.top + 10);
	}
	dc.SelectObject(oldPen1);
}