// ParticleBrowser.cpp : implementation file
//

#include "stdafx.h"
#include "ParticleBrowser.h"
#include "matsys_controls/particlepicker.h"
#include "matsys_controls/matsyscontrols.h"
#include "vgui/ISurface.h"
#include "vgui_controls/TextEntry.h"
#include "vgui_controls/Splitter.h"
#include "vgui_controls/Button.h"
#include "KeyValues.h"
#include "vgui/KeyCode.h"
#include "texturesystem.h"
#include "HammerVGui.h"

static constexpr LPCTSTR pszIniSection = "Particle Browser";

// CParticleBrowser dialog


class CParticleBrowserPanel : public vgui::EditablePanel
{
public:
	CParticleBrowserPanel( CParticleBrowser *pBrowser, const char *panelName, vgui::HScheme hScheme ) :
	  vgui::EditablePanel( NULL, panelName, hScheme )
	{
		m_pBrowser = pBrowser;
	}

	virtual	void OnSizeChanged(int newWide, int newTall)
	{
		// call Panel and not EditablePanel OnSizeChanged.
		Panel::OnSizeChanged(newWide, newTall);
	}

	virtual void OnCommand( const char *pCommand )
	{
		if ( Q_strcmp( pCommand, "OK" ) == 0 )
		{
			m_pBrowser->EndDialog( IDOK );
		}
		else if ( Q_strcmp( pCommand, "Cancel" ) == 0 )
		{
			m_pBrowser->EndDialog( IDCANCEL );
		}
	}

	virtual void OnKeyCodeTyped(vgui::KeyCode code)
	{
		vgui::EditablePanel::OnKeyCodeTyped( code );

		if ( code == KEY_ENTER )
		{
			m_pBrowser->EndDialog( IDOK );
		}
		else if ( code == KEY_ESCAPE )
		{
			m_pBrowser->EndDialog( IDCANCEL );
		}
	}

	virtual void OnMessage(const KeyValues *params, vgui::VPANEL ifromPanel)
	{
		vgui::EditablePanel::OnMessage( params, ifromPanel );

		if ( Q_strcmp( params->GetName(), "ParticleSystemSelectionChanged" ) == 0 )
		{
			m_pBrowser->UpdateStatusLine();
		}
	}

	/*virtual void Paint()
	{
		vgui::EditablePanel::Paint();

		static bool fu = false;

		if ( fu )
			return;

		fu = true;

		float* zpos = reinterpret_cast<float*>( reinterpret_cast<char*>( vgui::surface() ) + 66324 );

		CMatRenderContextPtr pRenderContext( materials );
		VPROF( "CMatSystemSurface::PaintTraverse popups loop" );
		int popups = vgui::surface()->GetPopupCount();

		for ( int i = popups - 1; i >= 0; --i )
		{
			vgui::VPANEL popupPanel = vgui::surface()->GetPopup( i );

			if ( !popupPanel || !vgui::ipanel()->IsPopup( popupPanel ) )
				continue;

			if ( !vgui::ipanel()->IsFullyVisible( popupPanel ) )
				continue;

			// This makes sure the drag/drop helper is always the first thing drawn
			bool bIsTopmostPopup = vgui::ipanel()->IsTopmostPopup( popupPanel );

			// set our z position
			pRenderContext->SetStencilReferenceValue( bIsTopmostPopup ? popups : i );

			*zpos = ((float)(i) / (float)popups);
			vgui::ipanel()->PaintTraverse( popupPanel, true );
		}

		fu = false;
	}*/

	CParticleBrowser *m_pBrowser;
};

IMPLEMENT_DYNAMIC(CParticleBrowser, CVguiDialog)
CParticleBrowser::CParticleBrowser(CWnd* pParent /*=NULL*/)
	: CVguiDialog(CParticleBrowser::IDD, pParent)
{
	m_pPicker = new CParticlePicker( NULL );
	m_pStatusLine = new vgui::TextEntry( NULL, "StatusLine" );

	m_pButtonOK = new vgui::Button( NULL, "OpenButton", "OK" );
	m_pButtonCancel = new vgui::Button( NULL, "CancelButton", "Cancel" );
}

CParticleBrowser::~CParticleBrowser()
{
	// CDialog isn't going to clean up its vgui children
	delete m_pPicker;
	delete m_pStatusLine;
	delete m_pButtonOK;
	delete m_pButtonCancel;
}

void CParticleBrowser::SetParticleSysName( const char *pParticleSysName )
{
	char pTempName[255];
	strcpy( pTempName, pParticleSysName );

	char * pSelectedParticleSys = strchr( pTempName, '/' );
	if( pSelectedParticleSys)
	{
		pSelectedParticleSys += 1;
		Q_FixSlashes( pSelectedParticleSys, '\\' );
	}

	m_pPicker->SelectParticleSys( pParticleSysName );
	m_pPicker->SetInitialSelection( pSelectedParticleSys );

	m_pStatusLine->SetText( pParticleSysName );
}

void CParticleBrowser::GetParticleSysName( char *pParticleName, int length )
{
	m_pPicker->GetSelectedParticleSysName( pParticleName, length );

	Q_FixSlashes( pParticleName, '/' );
}

void CParticleBrowser::UpdateStatusLine()
{
	char szParticle[1024];

	m_pPicker->GetSelectedParticleSysName( szParticle, sizeof(szParticle) );

	m_pStatusLine->SetText( szParticle );
}

void CParticleBrowser::SaveLoadSettings( bool bSave )
{
	CString	str;
	CRect	rect;
	CWinApp	*pApp = AfxGetApp();

	if ( bSave )
	{
		GetWindowRect(rect);
		str.Format("%d %d %d %d", rect.left, rect.top, rect.right, rect.bottom);
		pApp->WriteProfileString(pszIniSection, "Position", str);
		pApp->WriteProfileString(pszIniSection, "Filter", m_pPicker->GetFilter() );
	}
	else
	{
		str = pApp->GetProfileString(pszIniSection, "Position");

		if (!str.IsEmpty())
		{
			sscanf(str, "%d %d %d %d", &rect.left, &rect.top, &rect.right, &rect.bottom);
			MoveWindow(rect.left, rect.top, rect.right-rect.left, rect.bottom-rect.top, FALSE);
			Resize();
		}

		str = pApp->GetProfileString(pszIniSection, "Filter");

		if (!str.IsEmpty())
		{
			m_pPicker->SetFilter( str );
		}
	}
}



void CParticleBrowser::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}

void CParticleBrowser::Resize()
{
	// reposition controls
	CRect rect;
	GetClientRect( &rect );

	m_VGuiWindow.MoveWindow( rect );

	m_pPicker->SetBounds( 0, 0, rect.Width(), rect.Height() );
}

void CParticleBrowser::OnSize(UINT nType, int cx, int cy)
{
	if (nType == SIZE_MINIMIZED || !IsWindow(m_VGuiWindow.m_hWnd) )
	{
		CDialog::OnSize(nType, cx, cy);
		return;
	}

	Resize();

	CDialog::OnSize(nType, cx, cy);
}

BOOL CParticleBrowser::OnEraseBkgnd(CDC* pDC)
{
	return TRUE;
}

BEGIN_MESSAGE_MAP(CParticleBrowser, CVguiDialog)
	ON_WM_SIZE()
	ON_WM_DESTROY()
	ON_WM_ERASEBKGND()
END_MESSAGE_MAP()

BOOL CParticleBrowser::PreTranslateMessage( MSG* pMsg )
{
	// don't filter dialog message
	return CWnd::PreTranslateMessage( pMsg );
}

BOOL CParticleBrowser::OnInitDialog()
{
	CDialog::OnInitDialog();

	m_VGuiWindow.Create( NULL, _T("ParticleViewer"), WS_VISIBLE|WS_CHILD, CRect(0,0,100,100), this, IDD_PARTICLE_BROWSER );

	vgui::EditablePanel *pMainPanel = new CParticleBrowserPanel( this, "ParticleBrowerPanel", HammerVGui()->GetHammerScheme() );

	m_VGuiWindow.SetParentWindow( &m_VGuiWindow );
	m_VGuiWindow.SetMainPanel( pMainPanel );
	pMainPanel->MakePopup( false, false );
    m_VGuiWindow.SetRepaintInterval( 30 );

	m_pPicker->SetParent( pMainPanel );
	m_pPicker->AddActionSignalTarget( pMainPanel );

	m_pButtonOK->SetParent( pMainPanel );
	m_pButtonOK->AddActionSignalTarget( pMainPanel );
	m_pButtonOK->SetCommand( "OK" );

	m_pButtonCancel->SetParent( pMainPanel );
	m_pButtonCancel->AddActionSignalTarget( pMainPanel );
	m_pButtonCancel->SetCommand( "Cancel" );

	m_pStatusLine->SetParent( pMainPanel );
	m_pStatusLine->SetEditable( false );

	SaveLoadSettings( false ); // load

	auto s = new vgui::CBoxSizer( vgui::ESLD_VERTICAL );
	auto s2 = new vgui::CBoxSizer( vgui::ESLD_HORIZONTAL );
	s->AddPanel( m_pPicker, vgui::SizerAddArgs_t().Expand( 1.0f ).Padding( 0 ) );

	s2->AddPanel( m_pButtonOK, vgui::SizerAddArgs_t().Padding( 0 ) );
	s2->AddSpacer( vgui::SizerAddArgs_t().Padding( 2 ) );
	s2->AddPanel( m_pButtonCancel, vgui::SizerAddArgs_t().Padding( 0 ) );
	s2->AddSpacer( vgui::SizerAddArgs_t().Padding( 2 ) );
	s2->AddPanel( m_pStatusLine, vgui::SizerAddArgs_t().Expand( 1.0f ).Padding( 0 ) );

	s->AddSpacer( vgui::SizerAddArgs_t().Padding( 2 ) );
	s->AddSizer( s2, vgui::SizerAddArgs_t().Padding( 2 ) );

	pMainPanel->SetSizer( s );
	pMainPanel->InvalidateLayout( true );
	m_pPicker->Activate();

	Resize();

	return TRUE;
}

void CParticleBrowser::OnDestroy()
{
	SaveLoadSettings( true ); // save

	// model browser destoys our default cube map, reload it
	g_Textures.RebindDefaultCubeMap();

	CDialog::OnDestroy();
}

void CParticleBrowser::Show()
{
	if (m_pPicker)
	{
		m_pPicker->SetVisible( true );
	}
	if (m_pStatusLine)
		m_pStatusLine->SetVisible( true );
	if (m_pButtonOK)
		m_pButtonOK->SetVisible( true );
	if (m_pButtonCancel)
		m_pButtonCancel->SetVisible( true );

}
void CParticleBrowser::Hide()
{
	if (m_pPicker)
		m_pPicker->SetVisible( false );

	if (m_pStatusLine)
		m_pStatusLine->SetVisible( false );

	if (m_pButtonOK)
		m_pButtonOK->SetVisible( false );

	if (m_pButtonCancel)
		m_pButtonCancel->SetVisible( false );
}
