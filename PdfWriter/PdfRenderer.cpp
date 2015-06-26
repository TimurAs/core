#include "PdfRenderer.h"

#include "Src/Document.h"
#include "Src/Pages.h"
#include "Src/Image.h"
#include "Src/Font.h"
#include "Src/FontCidTT.h"

#include "../DesktopEditor/graphics/Image.h"
#include "../DesktopEditor/graphics/structures.h"
#include "../DesktopEditor/raster/BgraFrame.h"
#include "../DesktopEditor/raster/ImageFileFormatChecker.h"
#include "../DesktopEditor/cximage/CxImage/ximage.h"
#include "../DesktopEditor/fontengine/ApplicationFonts.h"
#include "../DesktopEditor/fontengine/FontManager.h"

#include "../DesktopEditor/common/File.h"
#include "../DesktopEditor/common/Directory.h"

#include "OnlineOfficeBinToPdf.h"


#define MM_2_PT(X) ((X) * 72.0 / 25.4)
#define PT_2_MM(X) ((X) * 25.4 / 72.0)

#define LONG_2_BOOL(X) ((X) ? true : false)

#ifdef DrawText
#undef DrawText
#endif

#ifdef LoadImage
#undef LoadImage
#endif

using namespace PdfWriter;

#define HI_SURROGATE_START  0xD800
#define HI_SURROGATE_END    0xDBFF
#define LO_SURROGATE_START  0xDC00
#define LO_SURROGATE_END    0xDFFF

// ���� ����� ����� ��� � ���������, �� �� ����������, ����� ��������� �� ����
static const long c_BrushTypeLinearGradient = 8001;
static const long c_BrushTypeRadialGradient = 8002;

enum ERendererCommandType
{
	renderercommandtype_Text  = 0x01,
	renderercommandtype_Image = 0x02,
	renderercommandtype_Path  = 0x03,
	renderercommandtype_Clip  = 0x04
};
//----------------------------------------------------------------------------------------
//
// CRendererCommandBase
//
//----------------------------------------------------------------------------------------
class CRendererCommandBase
{
public:
	virtual ~CRendererCommandBase(){};
	virtual ERendererCommandType GetType() = 0;
};
//----------------------------------------------------------------------------------------
//
// CRendererTextCommand
//
//----------------------------------------------------------------------------------------
#define RENDERERTEXTCOMMAND_FLAGS_FONT  0x0001
#define RENDERERTEXTCOMMAND_FLAGS_SIZE  0x0002
#define RENDERERTEXTCOMMAND_FLAGS_COLOR 0x0004
#define RENDERERTEXTCOMMAND_FLAGS_ALPHA 0x0008
#define RENDERERTEXTCOMMAND_FLAGS_SPACE 0x0010
class CRendererTextCommand : public CRendererCommandBase
{
public:
	CRendererTextCommand(unsigned char* pCodes, unsigned int nLen, const double& dX, const double& dY)
	{
		m_pCodes = pCodes;
		m_nLen   = nLen;
		m_dX     = dX;
		m_dY     = dY;
		m_nUpdateFlags = 0;
	}
	~CRendererTextCommand()
	{
		if (m_pCodes)
			delete[] m_pCodes;
	}
	ERendererCommandType GetType()
	{
		return renderercommandtype_Text;
	}

	inline double GetX() const
	{
		return m_dX;
	}
	inline double GetY() const
	{
		return m_dY;
	}
	inline unsigned char* GetCodes() const
	{
		return m_pCodes;
	}
	inline unsigned int GetCodesLen() const
	{
		return m_nLen;
	}
	void SetFont(CFontDict* pFont)
	{
		m_pFont = pFont;
		m_nUpdateFlags |= RENDERERTEXTCOMMAND_FLAGS_FONT;
	}
	void SetSize(const double& dSize)
	{
		m_dSize = dSize;
		m_nUpdateFlags |= RENDERERTEXTCOMMAND_FLAGS_SIZE;
	}
	void SetColor(const LONG& lColor)
	{
		m_lColor = lColor;
		m_nUpdateFlags |= RENDERERTEXTCOMMAND_FLAGS_COLOR;
	}
	void SetAlpha(const BYTE& nAlpha)
	{
		m_nAlpha = nAlpha;
		m_nUpdateFlags |= RENDERERTEXTCOMMAND_FLAGS_ALPHA;
	}
	void SetCharSpace(const double& dCharSpace)
	{
		m_dCharSpace = dCharSpace;
		m_nUpdateFlags |= RENDERERTEXTCOMMAND_FLAGS_SPACE;
	}
	inline bool IsPropertiesChanged() const
	{
		return (0 == m_nUpdateFlags ? false : true);
	}
	inline bool IsFontChanged() const
	{
		return LONG_2_BOOL(m_nUpdateFlags & RENDERERTEXTCOMMAND_FLAGS_FONT);
	}
	inline bool IsSizeChanged() const
	{
		return LONG_2_BOOL(m_nUpdateFlags & RENDERERTEXTCOMMAND_FLAGS_SIZE);
	}
	inline bool IsColorChanged() const
	{
		return LONG_2_BOOL(m_nUpdateFlags & RENDERERTEXTCOMMAND_FLAGS_COLOR);
	}
	inline bool IsAlphaChanged() const
	{
		return LONG_2_BOOL(m_nUpdateFlags & RENDERERTEXTCOMMAND_FLAGS_ALPHA);
	}
	inline bool IsSpaceChanged() const
	{
		return LONG_2_BOOL(m_nUpdateFlags & RENDERERTEXTCOMMAND_FLAGS_SPACE);
	}
	inline CFontDict* GetFont() const
	{
		return m_pFont;
	}
	inline double GetSize() const
	{
		return m_dSize;
	}
	inline LONG GetColor() const
	{
		return m_lColor;
	}
	inline BYTE GetAlpha() const
	{
		return m_nAlpha;
	}
	inline double GetSpace() const
	{
		return m_dCharSpace;
	}

private:

	unsigned char* m_pCodes;
	unsigned int   m_nLen;
	double         m_dX;
	double         m_dY;

	int            m_nUpdateFlags;

	CFontDict*     m_pFont;
	double         m_dSize;
	LONG           m_lColor;
	BYTE           m_nAlpha;
	double         m_dCharSpace;
};
//----------------------------------------------------------------------------------------
//
// CCommandManager
//
//----------------------------------------------------------------------------------------
CPdfRenderer::CCommandManager::CCommandManager(CPdfRenderer* pRenderer) : m_pRenderer(pRenderer)
{
}
CPdfRenderer::CCommandManager::~CCommandManager()
{
	Clear();
}
CRendererTextCommand* CPdfRenderer::CCommandManager::AddText(unsigned char* pCodes, unsigned int nLen, const double& dX, const double& dY)
{
	CRendererCommandBase* pCommand = new CRendererTextCommand(pCodes, nLen, dX, dY);
	Add(pCommand);
	return (CRendererTextCommand*)pCommand;
}
void CPdfRenderer::CCommandManager::Add(CRendererCommandBase* pCommand)
{
	if (pCommand)
	{
		if (m_vCommands.size() > 0 && pCommand->GetType() != m_vCommands.at(0)->GetType())
			Flush();

		m_vCommands.push_back(pCommand);
	}
}
void CPdfRenderer::CCommandManager::Flush()
{
	int nCommandsCount = m_vCommands.size();
	if (nCommandsCount > 0)
	{
		CPage* pPage = m_pRenderer->m_pPage;
		pPage->GrSave();

		if (!m_oTransform.IsIdentity())
			pPage->Concat(m_oTransform.m11, m_oTransform.m12, m_oTransform.m21, m_oTransform.m22, m_oTransform.dx, m_oTransform.dy);

		ERendererCommandType eType = m_vCommands.at(0)->GetType();
		if (renderercommandtype_Text == eType)
		{
			pPage->BeginText();
			CRendererTextCommand* pText = NULL;

			CFontDict* pTextFont = NULL;
			double     dTextSize = -1;
			LONG      lTextColor = 0;
			BYTE      nTextAlpha = 255;
			double    dTextSpace = 0;

			for (int nIndex = 0; nIndex < nCommandsCount; nIndex++)
			{
				pText = (CRendererTextCommand*)m_vCommands.at(nIndex);
				if (!pText)
					continue;

				//if (pText->IsPropertiesChanged())
				//{
				//	if (pText->IsFontChanged() || pText->IsSizeChanged())
				//		pPage->SetFontAndSize(pText->GetFont(), pText->GetSize());

				//	if (pText->IsColorChanged())
				//	{
				//		TColor oColor = pText->GetColor();
				//		pPage->SetFillColor(oColor.r, oColor.g, oColor.b);
				//	}

				//	if (pText->IsAlphaChanged())
				//		pPage->SetFillAlpha(pText->GetAlpha());

				//	if (pText->IsSpaceChanged())
				//		pPage->SetCharSpace(pText->GetSpace());
				//}

				if (pTextFont != pText->GetFont() || abs(dTextSize - pText->GetSize()) > 0.001)
				{
					pTextFont = pText->GetFont();
					dTextSize = pText->GetSize();
					pPage->SetFontAndSize(pTextFont, dTextSize);
				}

				if (lTextColor != pText->GetColor())
				{
					lTextColor = pText->GetColor();
					TColor oColor = lTextColor;
					pPage->SetFillColor(oColor.r, oColor.g, oColor.b);
				}

				if (nTextAlpha != pText->GetAlpha())
				{
					nTextAlpha = pText->GetAlpha();
					pPage->SetFillAlpha(nTextAlpha);
				}

				if (abs(dTextSpace - pText->GetSpace()) > 0.001)
				{
					dTextSpace = pText->GetSpace();
					pPage->SetCharSpace(dTextSpace);
				}

				pPage->DrawText(pText->GetX(), pText->GetY(), pText->GetCodes(), pText->GetCodesLen() * 2);
			}

			pPage->EndText();
		}

		pPage->GrRestore();
	}

	Clear();
}
void CPdfRenderer::CCommandManager::Clear()
{
	for (int nIndex = 0, nCount = m_vCommands.size(); nIndex < nCount; nIndex++)
	{
		CRendererCommandBase* pCommand = m_vCommands.at(nIndex);
		delete[] pCommand;
	}
	m_vCommands.clear();
}
void CPdfRenderer::CCommandManager::SetTransform(const CTransform& oTransform)
{
	m_oTransform = oTransform;
}
//----------------------------------------------------------------------------------------
//
// CPdfRenderer
//
//----------------------------------------------------------------------------------------
CPdfRenderer::CPdfRenderer(CApplicationFonts* pAppFonts) : m_oCommandManager(this)
{
	m_pAppFonts = pAppFonts;

	// ������� �������� ������� � ����������� �����
	m_pFontManager = pAppFonts->GenerateFontManager();
	CFontsCache* pMeasurerCache = new CFontsCache();
	pMeasurerCache->SetStreams(pAppFonts->GetStreams());
	m_pFontManager->SetOwnerCache(pMeasurerCache);

	m_pDocument = new CDocument();
	if (!m_pDocument || !m_pDocument->CreateNew())
	{
		SetError();
		return;
	}

	//m_pDocument->SetCompressionMode(COMP_ALL);

	m_bValid      = true;
	m_dPageHeight = 297;
	m_dPageWidth  = 210;
	m_pPage       = NULL;
	m_pFont       = NULL;

	m_nCounter = 0;
	m_nPagesCount = 0;

	m_bNeedUpdateTextFont      = true;
	m_bNeedUpdateTextColor     = true;
	m_bNeedUpdateTextAlpha     = true;
	m_bNeedUpdateTextCharSpace = true;
	m_bNeedUpdateTextSize      = true;

	m_wsTempFolder = L"";
	SetTempFolder(NSFile::CFileBinary::GetTempPath());
}
CPdfRenderer::~CPdfRenderer()
{
	RELEASEOBJECT(m_pDocument);
	RELEASEINTERFACE(m_pFontManager);

	if (L"" != m_wsTempFolder)
		NSDirectory::DeleteDirectory(m_wsTempFolder);
}
void CPdfRenderer::SaveToFile(const std::wstring& wsPath)
{
	if (!IsValid())
		return;

	m_oCommandManager.Flush();

	m_pDocument->SaveToFile(wsPath);
}
void CPdfRenderer::SetTempFolder(const std::wstring& wsPath)
{
	if (L"" != m_wsTempFolder)
		NSDirectory::DeleteDirectory(m_wsTempFolder);
	
	int nCounter = 0;
	m_wsTempFolder = wsPath + L"\\PDF\\";
	while (NSDirectory::Exists(m_wsTempFolder))
	{
		m_wsTempFolder = wsPath + L"\\PDF_" + std::to_wstring(nCounter) + L"\\";
		nCounter++;
	}
	NSDirectory::CreateDirectory(m_wsTempFolder);
}
std::wstring CPdfRenderer::GetTempFile()
{
	return NSFile::CFileBinary::CreateTempFileWithUniqueName(m_wsTempFolder, L"PDF");
}
void         CPdfRenderer::SetThemesPlace(const std::wstring& wsThemesPlace)
{
	m_wsThemesPlace = wsThemesPlace;
}
std::wstring CPdfRenderer::GetThemesPlace()
{
	return m_wsThemesPlace;
}
//----------------------------------------------------------------------------------------
// ��� ���������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::get_Type(LONG* lType)
{
	*lType = c_nPDFWriter;
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ �� ���������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::NewPage()
{
	m_oCommandManager.Flush();

	if (!IsValid())
		return S_FALSE;

	m_pPage = m_pDocument->AddPage();

	if (!m_pPage)
	{
		SetError();
		return S_FALSE;
	}

	m_pPage->SetWidth(m_dPageWidth);
	m_pPage->SetHeight(m_dPageHeight);

	m_oPen.Reset();
	m_oBrush.Reset();
	m_oFont.Reset();
	m_oPath.Clear();

	m_lClipDepth = 0;

	printf("Page %d\n", m_nPagesCount++);

	return S_OK;
}
HRESULT CPdfRenderer::get_Height(double* dHeight)
{
	*dHeight = m_dPageHeight;
	return S_OK;
}
HRESULT CPdfRenderer::put_Height(const double& dHeight)
{
	if (!IsValid() || !m_pPage)
		return S_FALSE;

	m_dPageHeight = dHeight;
	m_pPage->SetHeight(MM_2_PT(dHeight));
	return S_OK;
}
HRESULT CPdfRenderer::get_Width(double* dWidth)
{
	*dWidth = m_dPageWidth;
	return S_OK;
}
HRESULT CPdfRenderer::put_Width(const double& dWidth)
{
	if (!IsValid() || !m_pPage)
		return S_FALSE;

	m_dPageWidth = dWidth;
	m_pPage->SetWidth(MM_2_PT(dWidth));
	return S_OK;
}
HRESULT CPdfRenderer::get_DpiX(double* dDpiX)
{
	*dDpiX = 72;
	return S_OK;
}
HRESULT CPdfRenderer::get_DpiY(double* dDpiY)
{
	*dDpiY = 72;
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ � Pen
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::get_PenColor(LONG* lColor)
{
	*lColor = m_oPen.GetColor();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenColor(const LONG& lColor)
{
	m_oPen.SetColor(lColor);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenAlpha(LONG* lAlpha)
{
	*lAlpha = m_oPen.GetAlpha();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenAlpha(const LONG& lAlpha)
{
	m_oPen.SetAlpha(lAlpha);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenSize(double* dSize)
{
	*dSize = m_oPen.GetSize();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenSize(const double& dSize)
{
	m_oPen.SetSize(dSize);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenDashStyle(BYTE* nDashStyle)
{
	*nDashStyle = m_oPen.GetDashStyle();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenDashStyle(const BYTE& nDashStyle)
{
	m_oPen.SetDashStyle(nDashStyle);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenLineStartCap(BYTE* nCapStyle)
{
	*nCapStyle = m_oPen.GetStartCapStyle();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenLineStartCap(const BYTE& nCapStyle)
{
	m_oPen.SetStartCapStyle(nCapStyle);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenLineEndCap(BYTE* nCapStyle)
{
	*nCapStyle = m_oPen.GetEndCapStyle();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenLineEndCap(const BYTE& nCapStyle)
{
	m_oPen.SetEndCapStyle(nCapStyle);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenLineJoin(BYTE* nJoinStyle)
{
	*nJoinStyle = m_oPen.GetJoinStyle();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenLineJoin(const BYTE& nJoinStyle)
{
	m_oPen.SetJoinStyle(nJoinStyle);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenDashOffset(double* dOffset)
{
	*dOffset = m_oPen.GetDashOffset();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenDashOffset(const double& dOffset)
{
	m_oPen.SetDashOffset(dOffset);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenAlign(LONG* lAlign)
{
	*lAlign = m_oPen.GetAlign();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenAlign(const LONG& lAlign)
{
	m_oPen.SetAlign(lAlign);
	return S_OK;
}
HRESULT CPdfRenderer::get_PenMiterLimit(double* dMiter)
{
	*dMiter = m_oPen.GetMiter();
	return S_OK;
}
HRESULT CPdfRenderer::put_PenMiterLimit(const double& dMiter)
{
	m_oPen.SetMiter(dMiter);
	return S_OK;
}
HRESULT CPdfRenderer::PenDashPattern(double* pPattern, LONG lCount)
{
	m_oPen.SetDashPattern(pPattern, lCount);
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ � Brush
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::get_BrushType(LONG* lType)
{
	*lType = m_oBrush.GetType();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushType(const LONG& lType)
{
	m_oBrush.SetType(lType);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushColor1(LONG* lColor)
{
	*lColor = m_oBrush.GetColor1();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushColor1(const LONG& lColor)
{
	if (lColor != m_oBrush.GetColor1())
	{
		m_oBrush.SetColor1(lColor);
		m_bNeedUpdateTextColor = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushAlpha1(LONG* lAlpha)
{
	*lAlpha = m_oBrush.GetAlpha1();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushAlpha1(const LONG& lAlpha)
{
	if (lAlpha != m_oBrush.GetAlpha1())
	{
		m_oBrush.SetAlpha1(lAlpha);
		m_bNeedUpdateTextAlpha = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushColor2(LONG* lColor)
{
	*lColor = m_oBrush.GetColor2();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushColor2(const LONG& lColor)
{
	m_oBrush.SetColor2(lColor);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushAlpha2(LONG* lAlpha)
{
	*lAlpha = m_oBrush.GetAlpha2();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushAlpha2(const LONG& lAlpha)
{
	m_oBrush.SetAlpha2(lAlpha);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushTexturePath(std::wstring* wsPath)
{
	*wsPath = m_oBrush.GetTexturePath();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushTexturePath(const std::wstring& wsPath)
{
	m_oBrush.SetTexturePath(wsPath);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushTextureMode(LONG* lMode)
{
	*lMode = m_oBrush.GetTextureMode();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushTextureMode(const LONG& lMode)
{
	m_oBrush.SetTextureMode(lMode);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushTextureAlpha(LONG* lAlpha)
{
	*lAlpha = m_oBrush.GetTextureAlpha();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushTextureAlpha(const LONG& lAlpha)
{
	m_oBrush.SetTextureAlpha(lAlpha);
	return S_OK;
}
HRESULT CPdfRenderer::get_BrushLinearAngle(double* dAngle)
{
	*dAngle = m_oBrush.GetLinearAngle();
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushLinearAngle(const double& dAngle)
{
	m_oBrush.SetLinearAngle(dAngle);
	return S_OK;
}
HRESULT CPdfRenderer::BrushRect(const INT& nVal, const double& dLeft, const double& dTop, const double& dWidth, const double& dHeight)
{
	// ������� ����������� ����������, ������ ���� ������ ������� EnableBrushRect, ���� ������� �� ������, �����
	// ������������� �� ������� ����.
	m_oBrush.SetBrushRect(nVal, dLeft, dTop, dWidth, dHeight);
	return S_OK;
}
HRESULT CPdfRenderer::BrushBounds(const double& dLeft, const double& dTop, const double& dWidth, const double& dHeight)
{
	// TODO: ���� ������������ ��� �� �������� ����
	return S_OK;
}
HRESULT CPdfRenderer::put_BrushGradientColors(LONG* lColors, double* pPositions, LONG lCount)
{
	m_oBrush.SetGradientColors(lColors, pPositions, lCount);
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ �� ��������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::get_FontName(std::wstring* wsName)
{
	*wsName = m_oFont.GetName();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontName(const std::wstring& wsName)
{
	if (wsName != m_oFont.GetName())
	{
		m_oFont.SetName(wsName);
		m_bNeedUpdateTextFont = true;
	}

	return S_OK;
}
HRESULT CPdfRenderer::get_FontPath(std::wstring* wsPath)
{
	*wsPath = m_oFont.GetPath();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontPath(const std::wstring& wsPath)
{
	if (wsPath != m_oFont.GetPath())
	{
		m_oFont.SetPath(wsPath);
		m_bNeedUpdateTextFont = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_FontSize(double* dSize)
{
	*dSize = m_oFont.GetSize();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontSize(const double& dSize)
{
	if (abs(dSize - m_oFont.GetSize()) > 0.001)
	{
		m_oFont.SetSize(dSize);
		m_bNeedUpdateTextSize = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_FontStyle(LONG* lStyle)
{
	*lStyle = m_oFont.GetStyle();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontStyle(const LONG& lStyle)
{
	if (lStyle != m_oFont.GetStyle())
	{
		m_oFont.SetStyle(lStyle);
		m_bNeedUpdateTextFont = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_FontStringGID(INT* bGid)
{
	*bGid = m_oFont.GetGid() ? 1 : 0;
	return S_OK;
}
HRESULT CPdfRenderer::put_FontStringGID(const INT& bGid)
{
	m_oFont.SetGid(bGid ? true : false);
	return S_OK;
}
HRESULT CPdfRenderer::get_FontCharSpace(double* dSpace)
{
	*dSpace = m_oFont.GetCharSpace();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontCharSpace(const double& dSpace)
{
	if (abs(dSpace - m_oFont.GetCharSpace()) > 0.001)
	{
		m_oFont.SetCharSpace(dSpace);
		m_bNeedUpdateTextCharSpace = true;
	}
	return S_OK;
}
HRESULT CPdfRenderer::get_FontFaceIndex(int* nFaceIndex)
{
	*nFaceIndex = (int)m_oFont.GetFaceIndex();
	return S_OK;
}
HRESULT CPdfRenderer::put_FontFaceIndex(const int& nFaceIndex)
{
	if (nFaceIndex != m_oFont.GetFaceIndex())
	{
		m_oFont.SetFaceIndex(nFaceIndex);
		m_bNeedUpdateTextFont = true;
	}

	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ ������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::CommandDrawTextCHAR(const LONG& lUnicode, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset)
{
	// TODO: �����������
	return S_OK;
}
HRESULT CPdfRenderer::CommandDrawText(const std::wstring& wsUnicodeText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset)
{
	if (!IsPageValid() || !wsUnicodeText.size())
		return S_FALSE;

	unsigned int* pUnicodes = new unsigned int[wsUnicodeText.size()];
	if (!pUnicodes)
		return S_FALSE;

	unsigned int* pOutput = pUnicodes;
	unsigned int unLen = 0;
	if (2 == sizeof(wchar_t))
	{
		const wchar_t* wsEnd = wsUnicodeText.c_str() + wsUnicodeText.size();
		wchar_t* wsInput = (wchar_t*)wsUnicodeText.c_str();

		wchar_t wLeading, wTrailing;
		unsigned int unCode;
		while (wsInput < wsEnd)
		{
			wLeading = *wsInput++;
			if (wLeading < 0xD800 || wLeading > 0xDFFF)
			{
				pUnicodes[unLen++] = (unsigned int)wLeading;
			}
			else if (wLeading >= 0xDC00)
			{
				// ������ �� ������ ����
				continue;
			}
			else
			{
				unCode = (wLeading & 0x3FF) << 10;
				wTrailing = *wsInput++;
				if (wTrailing < 0xDC00 || wTrailing > 0xDFFF)
				{
					// ������ �� ������ ����
					continue;
				}
				else
				{
					pUnicodes[unLen++] = (unCode | (wTrailing & 0x3FF) + 0x10000);
				}
			}
		}
	}
	else
	{
		unLen = wsUnicodeText.size();
		for (unsigned int unIndex = 0; unIndex < unLen; unIndex++)
		{
			pUnicodes[unIndex] = (unsigned int)wsUnicodeText.at(unIndex);
		}		
	}

	if (m_bNeedUpdateTextFont)
		UpdateFont();

	if (!m_pFont)
		return S_FALSE;

	unsigned char* pCodes = m_pFont->EncodeString(pUnicodes, unLen);
	delete[] pUnicodes;

	CRendererTextCommand* pText = m_oCommandManager.AddText(pCodes, unLen * 2, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY));
	pText->SetFont(m_pFont);
	pText->SetSize(m_oFont.GetSize());
	pText->SetColor(m_oBrush.GetColor1());
	pText->SetAlpha((BYTE)m_oBrush.GetAlpha1());
	pText->SetCharSpace(m_oFont.GetCharSpace());

	return S_OK;
}
HRESULT CPdfRenderer::CommandDrawTextExCHAR(const LONG& lUnicode, const LONG& lGid, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	// TODO: �����������
	return S_OK;
}
HRESULT CPdfRenderer::CommandDrawTextEx(const std::wstring& wsUnicodeText, const std::wstring& wsGidText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	// TODO: �����������
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::BeginCommand(const DWORD& dwType)
{
	// ����� �� ������ �� ������
	return S_OK;
}
HRESULT CPdfRenderer::EndCommand(const DWORD& dwType)
{
	if (!IsPageValid())
		return S_FALSE;

	// ����� �� ��������� ���� 2 �������: ������������ ������� ��� � ����� � �������� ����
	if (c_nClipType == dwType)
	{
		m_oCommandManager.Flush();
		m_pPage->GrSave();
		m_lClipDepth++;
		UpdateTransform();

		if (c_nClipRegionTypeWinding == c_nClipRegionTypeWinding)
			m_oPath.Clip(m_pPage, false);
		else
			m_oPath.Clip(m_pPage, true);
	}
	else if (c_nResetClipType == dwType)
	{
		m_oCommandManager.Flush();
		while (m_lClipDepth)
		{
			m_pPage->GrRestore();
			m_lClipDepth--;
		}
	}

	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ � �����
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::PathCommandStart()
{
	m_oPath.Clear();
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandEnd()
{
	m_oPath.Clear();
	return S_OK;
}
HRESULT CPdfRenderer::DrawPath(const LONG& lType)
{
	m_oCommandManager.Flush();

	if (!IsPageValid())
		return S_FALSE;

	bool bStroke = LONG_2_BOOL(lType & c_nStroke);
	bool bFill   = LONG_2_BOOL(lType & c_nWindingFillMode);
	bool bEoFill = LONG_2_BOOL(lType & c_nEvenOddFillMode);

	m_pPage->GrSave();
	UpdateTransform();

	if (bStroke)
		UpdatePen();

	if (bFill || bEoFill)
		UpdateBrush();

	if (!m_pShading)
	{
		m_oPath.Draw(m_pPage, bStroke, bFill, bEoFill);
	}
	else
	{
		if (bFill || bEoFill)
		{
			m_pPage->GrSave();
			m_oPath.Clip(m_pPage, bEoFill);

			if (NULL != m_pShadingExtGrState)
				m_pPage->SetExtGrState(m_pShadingExtGrState);

			m_pPage->DrawShading(m_pShading);
			m_pPage->GrRestore();
		}

		if (bStroke)
			m_oPath.Draw(m_pPage, bStroke, false, false);
	}

	m_pPage->GrRestore();

	return S_OK;
}
HRESULT CPdfRenderer::PathCommandMoveTo(const double& dX, const double& dY)
{
	m_oPath.MoveTo(MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY));
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandLineTo(const double& dX, const double& dY)
{
	m_oPath.LineTo(MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY));
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandLinesTo(double* pPoints, const int& nCount)
{
	if (nCount < 4 || !pPoints)
		return S_OK;

	if (!m_oPath.IsMoveTo())
		m_oPath.MoveTo(MM_2_PT(pPoints[0]), MM_2_PT(m_dPageHeight - pPoints[1]));

	int nPointsCount = (nCount / 2) - 1;
	for (int nIndex = 1; nIndex <= nPointsCount; ++nIndex)
	{
		m_oPath.LineTo(MM_2_PT(pPoints[nIndex * 2]), MM_2_PT(m_dPageHeight - pPoints[nIndex * 2 + 1]));
	}

	return S_OK;
}
HRESULT CPdfRenderer::PathCommandCurveTo(const double& dX1, const double& dY1, const double& dX2, const double& dY2, const double& dXe, const double& dYe)
{
	m_oPath.CurveTo(MM_2_PT(dX1), MM_2_PT(m_dPageHeight - dY1), MM_2_PT(dX2), MM_2_PT(m_dPageHeight - dY2), MM_2_PT(dXe), MM_2_PT(m_dPageHeight - dYe));
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandCurvesTo(double* pPoints, const int& nCount)
{
	if (nCount < 8 || !pPoints)
		return S_OK;

	if (!m_oPath.IsMoveTo())
		m_oPath.MoveTo(MM_2_PT(pPoints[0]), MM_2_PT(m_dPageHeight - pPoints[1]));

	int nPointsCount = (nCount - 2) / 6;
	double* pCur = pPoints + 2;
	for (int nIndex = 0; nIndex <= nPointsCount; ++nIndex, pCur += 6)
	{
		m_oPath.CurveTo(MM_2_PT(pCur[0]), MM_2_PT(m_dPageHeight - pCur[1]), MM_2_PT(pCur[2]), MM_2_PT(m_dPageHeight - pCur[3]), MM_2_PT(pCur[4]), MM_2_PT(m_dPageHeight - pCur[5]));
	}

	return S_OK;
}
HRESULT CPdfRenderer::PathCommandArcTo(const double& dX, const double& dY, const double& dW, const double& dH, const double& dStartAngle, const double& dSweepAngle)
{
	m_oPath.ArcTo(MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY - dH), MM_2_PT(dW), MM_2_PT(dH), dStartAngle, dSweepAngle);
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandClose()
{
	m_oPath.Close();
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandGetCurrentPoint(double* dX, double* dY)
{
	m_oPath.GetLastPoint(*dX, *dY);
	*dX = PT_2_MM(*dX);
	*dY = PT_2_MM(*dY);
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandTextCHAR(const LONG& lUnicode, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset)
{
	m_oPath.AddText(m_oFont, lUnicode, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY), MM_2_PT(dW), MM_2_PT(dH), MM_2_PT(dBaselineOffset));
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandText(const std::wstring& wsText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset)
{
	m_oPath.AddText(m_oFont, wsText, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY), MM_2_PT(dW), MM_2_PT(dH), MM_2_PT(dBaselineOffset));
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandTextExCHAR(const LONG& lUnicode, const LONG& lGid, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	m_oPath.AddText(m_oFont, lUnicode, lGid, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY), MM_2_PT(dW), MM_2_PT(dH), MM_2_PT(dBaselineOffset), dwFlags);
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandTextEx(const std::wstring& wsUnicodeText, const std::wstring& wsGidText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	m_oPath.AddText(m_oFont, wsUnicodeText, wsGidText, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY), MM_2_PT(dW), MM_2_PT(dH), MM_2_PT(dBaselineOffset), dwFlags);
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ������ �����������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::DrawImage(IGrObject* pImage, const double& dX, const double& dY, const double& dW, const double& dH)
{
	m_oCommandManager.Flush();

	if (!IsPageValid() || !pImage)
		return S_OK;

	if (!DrawImage((Aggplus::CImage*)pImage, dX, dY, dW, dH, 255))
		return S_FALSE;

	return S_OK;
}
HRESULT CPdfRenderer::DrawImageFromFile(const std::wstring& wsImagePath, const double& dX, const double& dY, const double& dW, const double& dH, const BYTE& nAlpha)
{
	m_oCommandManager.Flush();

	if (!IsPageValid())
		return S_OK;

	Aggplus::CImage oAggImage(wsImagePath);
	if (!DrawImage(&oAggImage, dX, dY, dW, dH, nAlpha))
		return S_FALSE;

	return S_OK;
}
//----------------------------------------------------------------------------------------
// ������� ��� ����������� ��������������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::SetTransform(const double& dM11, const double& dM12, const double& dM21, const double& dM22, const double& dX, const double& dY)
{
	m_oCommandManager.Flush();
	m_oTransform.Set(dM11, dM12, dM21, dM22, dX, dY);
	return S_OK;
}
HRESULT CPdfRenderer::GetTransform(double* dM11, double* dM12, double* dM21, double* dM22, double* dX, double* dY)
{
	*dM11 = m_oTransform.m11;
	*dM12 = m_oTransform.m12;
	*dM21 = m_oTransform.m21;
	*dM22 = m_oTransform.m22;
	*dX   = m_oTransform.dx;
	*dY   = m_oTransform.dy;
	return S_OK;
}
HRESULT CPdfRenderer::ResetTransform()
{
	m_oCommandManager.Flush();
	m_oTransform.Reset();
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ��� �����
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::get_ClipMode(LONG* lMode)
{
	*lMode = m_lClipMode;
	return S_OK;
}
HRESULT CPdfRenderer::put_ClipMode(const LONG& lMode)
{
	m_lClipMode = lMode;
	return S_OK;
}
//----------------------------------------------------------------------------------------
// �������������� �������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::CommandLong(const LONG& lType, const LONG& lCommand)
{
	return S_OK;
}
HRESULT CPdfRenderer::CommandDouble(const LONG& lType, const double& dCommand)
{
	return S_OK;
}
HRESULT CPdfRenderer::CommandString(const LONG& lType, const std::wstring& sCommand)
{
	return S_OK;
}
//----------------------------------------------------------------------------------------
// �������������� ������� Pdf ���������
//----------------------------------------------------------------------------------------
HRESULT CPdfRenderer::CommandDrawTextPdf(const std::wstring& bsUnicodeText, const std::wstring& bsGidText, const std::wstring& wsSrcCodeText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	return S_OK;
}
HRESULT CPdfRenderer::PathCommandTextPdf(const std::wstring& bsUnicodeText, const std::wstring& bsGidText, const std::wstring& bsSrcCodeText, const double& dX, const double& dY, const double& dW, const double& dH, const double& dBaselineOffset, const DWORD& dwFlags)
{
	return S_OK;
}
HRESULT CPdfRenderer::DrawImage1bpp(Pix* pImageBuffer, const unsigned int& unWidth, const unsigned int& unHeight, const double& dX, const double& dY, const double& dW, const double& dH)
{
	m_oCommandManager.Flush();

	if (!IsPageValid() || !pImageBuffer)
		return S_OK;

	m_pPage->GrSave();
	UpdateTransform();
	
	CImageDict* pPdfImage = m_pDocument->CreateImage();
	pPdfImage->LoadBW(pImageBuffer, unWidth, unHeight);
	m_pPage->DrawImage(pPdfImage, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY - dH), MM_2_PT(dW), MM_2_PT(dH));

	m_pPage->GrRestore();
	return S_OK;
}
HRESULT CPdfRenderer::EnableBrushRect(const LONG& lEnable)
{
	m_oBrush.EnableBrushRect(LONG_2_BOOL(lEnable));
	return S_OK;
}
HRESULT CPdfRenderer::SetLinearGradient(const double& dX0, const double& dY0, const double& dX1, const double& dY1)
{
	m_oBrush.SetType(c_BrushTypeLinearGradient);
	m_oBrush.SetLinearGradientPattern(dX0, dY0, dX1, dY1);
	return S_OK;
}
HRESULT CPdfRenderer::SetRadialGradient(const double& dX0, const double& dY0, const double& dR0, const double& dX1, const double& dY1, const double& dR1)
{
	m_oBrush.SetType(c_BrushTypeRadialGradient);
	m_oBrush.SetRadialGradientPattern(dX0, dY0, dR0, dX1, dY1, dR1);
	return S_OK;
}
//----------------------------------------------------------------------------------------
// ���������� �������
//----------------------------------------------------------------------------------------
PdfWriter::CImageDict* CPdfRenderer::LoadImage(Aggplus::CImage* pImage, const BYTE& nAlpha)
{
	int nImageW = abs((int)pImage->GetWidth());
	int nImageH = abs((int)pImage->GetHeight());
	BYTE* pData = pImage->GetData();
	int nStride = 4 * nImageW;

	// �������� ������ ��������� �������� ������ ������ Jpeg2000
	bool bJpeg = false;
	if (nImageH < 100 || nImageW < 100)
		bJpeg = true;

	// TODO: ���� �� ���������� ��� � CxImage ��������� ����������� ����������� ������ ������ � Jpeg2000,
	//       �.�. ����� ���������� ������� ������ � ����������� ���� ������� ������.
	bJpeg = true;

	// ����������� �� �������� � ���������� ���� �� � ��� �����-�����
	bool bAlpha = false;
	for (int nIndex = 0, nSize = nImageW * nImageH; nIndex < nSize; nIndex++)
	{
		if (pData[4 * nIndex + 3] < 255)
		{
			bAlpha = true;
			break;
		}
	}

	CxImage oCxImage;
	if (!oCxImage.CreateFromArray(pData, nImageW, nImageH, 32, nStride, (pImage->GetStride() >= 0) ? true : false))
		return NULL;

	oCxImage.SetJpegQualityF(85.0f);

	BYTE* pBuffer = NULL;
	int nBufferSize = 0;
	if (!oCxImage.Encode(pBuffer, nBufferSize, bJpeg ? CXIMAGE_FORMAT_JPG : CXIMAGE_FORMAT_JP2))
		return NULL;

	if (!pBuffer || !nBufferSize)
		return NULL;

	CImageDict* pPdfImage = m_pDocument->CreateImage();
	if (bAlpha || nAlpha < 255)
		pPdfImage->LoadSMask(pData, nImageW, nImageH, nAlpha);

	if (bJpeg)
		pPdfImage->LoadJpeg(pBuffer, nBufferSize, nImageW, nImageH);
	else
		pPdfImage->LoadJpx(pBuffer, nBufferSize, nImageW, nImageH);

	free(pBuffer);

	return pPdfImage;
}
bool CPdfRenderer::DrawImage(Aggplus::CImage* pImage, const double& dX, const double& dY, const double& dW, const double& dH, const BYTE& nAlpha)
{
	CImageDict* pPdfImage = LoadImage(pImage, nAlpha);
	if (!pPdfImage)
		return false;

	m_pPage->GrSave();
	UpdateTransform();
	m_pPage->DrawImage(pPdfImage, MM_2_PT(dX), MM_2_PT(m_dPageHeight - dY - dH), MM_2_PT(dW), MM_2_PT(dH));
	m_pPage->GrRestore();
	
	return true;
}
void CPdfRenderer::UpdateFont()
{
	m_bNeedUpdateTextFont = false;
	std::wstring& wsFontPath = m_oFont.GetPath();
	LONG lFaceIndex = m_oFont.GetFaceIndex();
	if (L"" == wsFontPath)
	{
		std::wstring& wsFontName = m_oFont.GetName();
		bool bBold   = m_oFont.IsBold();
		bool bItalic = m_oFont.IsItalic();
		bool bFind = false;
		for (int nIndex = 0, nCount = m_vFonts.size(); nIndex < nCount; nIndex++)
		{
			TFontInfo& oInfo = m_vFonts.at(nIndex);
			if (oInfo.wsFontName == wsFontName && oInfo.bBold == bBold && oInfo.bItalic == bItalic)
			{
				wsFontPath = oInfo.wsFontPath;
				lFaceIndex = oInfo.lFaceIndex;
				bFind = true;
				break;
			}
		}

		if (!bFind)
		{
			CFontSelectFormat oFontSelect;
			oFontSelect.wsName = new std::wstring(m_oFont.GetName());
			oFontSelect.bItalic = new INT(m_oFont.IsItalic() ? 1 : 0);
			oFontSelect.bBold   = new INT(m_oFont.IsBold() ? 1 : 0);
			CFontInfo* pFontInfo = m_pFontManager->GetFontInfoByParams(oFontSelect);

			wsFontPath = pFontInfo->m_wsFontPath;
			lFaceIndex = pFontInfo->m_lIndex;

			m_vFonts.push_back(TFontInfo(wsFontName, bBold, bItalic, wsFontPath, lFaceIndex));
		}
	}

	m_pFont = NULL;
	if (L"" != wsFontPath)
	{
		// TODO: ���� �� ����� ������������, ��� ������ ������ ���� TrueType, ���� OpenType
		m_pFontManager->LoadFontFromFile(wsFontPath, lFaceIndex, 10, 72, 72);
		std::wstring wsFontType = m_pFontManager->GetFontType();
		if (L"TrueType" == wsFontType || L"OpenType" == wsFontType || L"CFF" == wsFontType)
			m_pFont = m_pDocument->CreateTrueTypeFont(wsFontPath, lFaceIndex);
	}
}
void CPdfRenderer::UpdateTransform()
{
	CTransform& t = m_oTransform;
	m_pPage->Concat(t.m11, -t.m12, -t.m21, t.m22, MM_2_PT(t.dx + t.m21 * m_dPageHeight), MM_2_PT(m_dPageHeight - m_dPageHeight * t.m22 - t.dy));
}
void CPdfRenderer::UpdatePen()
{
	TColor& oColor = m_oPen.GetTColor();
	m_pPage->SetStrokeColor(oColor.r, oColor.g, oColor.b);
	m_pPage->SetStrokeAlpha((unsigned char)m_oPen.GetAlpha());
	m_pPage->SetLineWidth(MM_2_PT(m_oPen.GetSize()));
	
	LONG lDashCount = 0;
	double* pDashPattern = NULL;
	
	LONG lDashStyle = m_oPen.GetDashStyle();
	if (Aggplus::DashStyleSolid == lDashStyle)
	{
		// ������ �� ������
	}
	else if (Aggplus::DashStyleCustom == lDashStyle)
	{
		double *pDashPatternMM = m_oPen.GetDashPattern(lDashCount);
		if (pDashPatternMM && lDashCount)
		{
			pDashPattern = new double[lDashCount];
			if (pDashPattern)
			{
				for (LONG lIndex = 0; lIndex < lDashCount; lIndex++)
				{
					pDashPattern[lIndex] = MM_2_PT(pDashPatternMM[lIndex]);
				}
			}
		}
	}
	else
	{
		// TODO: ����������� ������ ���� ���������� �����
	}

	if (pDashPattern && lDashCount)
	{
		m_pPage->SetDash(pDashPattern, lDashCount, MM_2_PT(m_oPen.GetDashOffset()));
		delete[] pDashPattern;
	}

	LONG lCapStyle = m_oPen.GetStartCapStyle();
	if (Aggplus::LineCapRound == lCapStyle)
		m_pPage->SetLineCap(linecap_Round);
	else if (Aggplus::LineCapSquare)
		m_pPage->SetLineCap(linecap_ProjectingSquare);
	else
		m_pPage->SetLineCap(linecap_Butt);

	LONG lJoinStyle = m_oPen.GetJoinStyle();
	if (Aggplus::LineJoinBevel == lJoinStyle)
		m_pPage->SetLineJoin(linejoin_Bevel);
	else if (Aggplus::LineJoinMiter == lJoinStyle)
	{
		m_pPage->SetLineJoin(linejoin_Miter);
		m_pPage->SetMiterLimit(MM_2_PT(m_oPen.GetMiter()));
	}
	else
		m_pPage->SetLineJoin(linejoin_Round);
}
void CPdfRenderer::UpdateBrush()
{
	m_pShading = NULL;
	m_pShadingExtGrState = NULL;

	LONG lBrushType = m_oBrush.GetType();
	if (c_BrushTypeTexture == lBrushType)
	{
		std::wstring& wsTexturePath = m_oBrush.GetTexturePath();
		CImageFileFormatChecker oImageFormat(wsTexturePath);

		CImageDict* pImage = NULL;
		int nImageW = 0;
		int nImageH = 0;
		if (_CXIMAGE_FORMAT_JPG == oImageFormat.eFileType || _CXIMAGE_FORMAT_JP2 == oImageFormat.eFileType)
		{
			pImage = m_pDocument->CreateImage();
			CBgraFrame oFrame;
			oFrame.OpenFile(wsTexturePath);
			nImageH = oFrame.get_Height();
			nImageW = oFrame.get_Width();

			if (pImage)
			{
				if (_CXIMAGE_FORMAT_JPG == oImageFormat.eFileType)
					pImage->LoadJpeg(wsTexturePath.c_str(), nImageW, nImageH);
				else
					pImage->LoadJpx(wsTexturePath.c_str(), nImageW, nImageH);
			}
		}
		else
		{
			Aggplus::CImage oImage(wsTexturePath);
			nImageW = abs((int)oImage.GetWidth());
			nImageH = abs((int)oImage.GetHeight());
			pImage = LoadImage(&oImage, 255);
		}

		if (pImage)
		{		
			LONG lTextureMode = m_oBrush.GetTextureMode();

			double dW = 10;
			double dH = 10;

			double dL, dR, dT, dB;
			m_oPath.GetBounds(dL, dT, dR, dB);

			if (c_BrushTextureModeStretch == lTextureMode)
			{
				// ����������� �������� �� �������� ����
				dW = max(10, dR - dL);
				dH = max(10, dB - dT);
			}
			else
			{
				// ������� �������� ������ � ��������. ������� ����� - ��� ������� �������� � �������.
				dW = nImageW * 72 / 96;
				dH = nImageH * 72 / 96;
			}			

			// ��� �����, ����� ����� ������ ���� ������ ������ ���� ������� ������ �������� ��� ������� ��������������.
			CMatrix* pMatrix = m_pPage->GetTransform();
			pMatrix->Apply(dL, dB);
			CMatrix oPatternMatrix = *pMatrix;
			oPatternMatrix.x = dL;
			oPatternMatrix.y = dB;
			m_pPage->SetPatternColorSpace(m_pDocument->CreateImageTilePattern(dW, dH, pImage, &oPatternMatrix));
		}
	}
	else if (c_BrushTypeHatch1 == lBrushType)
	{
		std::wstring& wsHatchType = m_oBrush.GetTexturePath();

		double dW = 8 * 72 / 96;
		double dH = 8 * 72 / 96;

		TColor& oColor1 = m_oBrush.GetTColor1();
		TColor& oColor2 = m_oBrush.GetTColor2();
		BYTE nAlpha1 = (BYTE)m_oBrush.GetAlpha1();
		BYTE nAlpha2 = (BYTE)m_oBrush.GetAlpha2();

		m_pPage->SetPatternColorSpace(m_pDocument->CreateHatchPattern(dW, dH, oColor1.r, oColor1.g, oColor1.b, nAlpha1, oColor2.r, oColor2.g, oColor2.b, nAlpha2, wsHatchType));
	}
	else if (c_BrushTypeRadialGradient == lBrushType || c_BrushTypeLinearGradient == lBrushType)
	{
		TColor* pGradientColors;
		double* pPoints;
		LONG lCount;

		m_oBrush.GetGradientColors(pGradientColors, pPoints, lCount);

		if (lCount > 0)
		{
			unsigned char* pColors = new unsigned char[3 * lCount];
			unsigned char* pAlphas = new unsigned char[lCount];
			if (pColors)
			{
				for (LONG lIndex = 0; lIndex < lCount; lIndex++)
				{
					pColors[3 * lIndex + 0] = pGradientColors[lIndex].r;
					pColors[3 * lIndex + 1] = pGradientColors[lIndex].g;
					pColors[3 * lIndex + 2] = pGradientColors[lIndex].b;
					pAlphas[lIndex]         = pGradientColors[lIndex].a;
				}

				if (c_BrushTypeLinearGradient == lBrushType)
				{
					double dX0, dY0, dX1, dY1;
					m_oBrush.GetLinearGradientPattern(dX0, dY0, dX1, dY1);
					m_pShading = m_pDocument->CreateAxialShading(m_pPage, MM_2_PT(dX0), MM_2_PT(m_dPageHeight - dY0), MM_2_PT(dX1), MM_2_PT(m_dPageHeight - dY1), pColors, pAlphas, pPoints, lCount, m_pShadingExtGrState);
				}
				else //if (c_BrushTypeRadialGradient == lBrushType)
				{
					double dX0, dY0, dR0, dX1, dY1, dR1;
					m_oBrush.GetRadialGradientPattern(dX0, dY0, dR0, dX1, dY1, dR1);
					m_pShading = m_pDocument->CreateRadialShading(m_pPage, MM_2_PT(dX0), MM_2_PT(m_dPageHeight - dY0), MM_2_PT(dR0), MM_2_PT(dX1), MM_2_PT(m_dPageHeight - dY1), MM_2_PT(dR1), pColors, pAlphas, pPoints, lCount, m_pShadingExtGrState);
				}
				delete[] pColors;
			}
		}
	}
	else// if (c_BrushTypeSolid == lBrushType)
	{
		TColor& oColor1 = m_oBrush.GetTColor1();
		m_pPage->SetFillColor(oColor1.r, oColor1.g, oColor1.b);
		m_pPage->SetFillAlpha((unsigned char)m_oBrush.GetAlpha1());
	}
}
HRESULT CPdfRenderer::OnlineWordToPdf          (const std::wstring& wsSrcFile, const std::wstring& wsDstFile)
{
	if (!NSOnlineOfficeBinToPdf::ConvertBinToPdf(this, wsSrcFile, wsDstFile, false))
		return S_FALSE;

	return S_OK;
}
HRESULT CPdfRenderer::OnlineWordToPdfFromBinary(const std::wstring& wsSrcFile, const std::wstring& wsDstFile)
{
	if (!NSOnlineOfficeBinToPdf::ConvertBinToPdf(this, wsSrcFile, wsDstFile, true))
		return S_FALSE;

	return S_OK;
}

static inline void UpdateMaxMinPoints(double& dMinX, double& dMinY, double& dMaxX, double& dMaxY, const double& dX, const double& dY)
{
	if (dX < dMinX)
		dMinX = dX;

	if (dX > dMaxX)
		dMaxX = dX;

	if (dY < dMinY)
		dMinY = dY;

	if (dY > dMaxY)
		dMaxY = dY;
}
void CPdfRenderer::CPath::Draw(PdfWriter::CPage* pPage, bool bStroke, bool bFill, bool bEoFill)
{
	for (int nIndex = 0, nCount = m_vCommands.size(); nIndex < nCount; nIndex++)
	{
		CPathCommandBase* pCommand = m_vCommands.at(nIndex);
		pCommand->Draw(pPage);
	}

	if (bStroke && !bFill && !bEoFill)
		pPage->Stroke();
	else if (bStroke && bFill)
		pPage->FillStroke();
	else if (bStroke && bEoFill)
		pPage->EoFillStroke();
	else if (bFill)
		pPage->Fill();
	else if (bEoFill)
		pPage->EoFill();
	else
		pPage->EndPath();
}
void CPdfRenderer::CPath::Clip(PdfWriter::CPage* pPage, bool bEvenOdd)
{
	for (int nIndex = 0, nCount = m_vCommands.size(); nIndex < nCount; nIndex++)
	{
		CPathCommandBase* pCommand = m_vCommands.at(nIndex);
		pCommand->Draw(pPage);
	}

	if (bEvenOdd)
		pPage->Eoclip();
	else
		pPage->Clip();

	pPage->EndPath();
}
void CPdfRenderer::CPath::GetLastPoint(double& dX, double& dY)
{
	dX = 0;
	dY = 0;

	bool bFindMoveTo = false;
	for (int nIndex = m_vCommands.size() - 1; nIndex >= 0; nIndex--)
	{
		CPathCommandBase* pCommand = m_vCommands.at(nIndex);
		if (rendererpathcommand_Close == pCommand->GetType())
		{
			bFindMoveTo = true;
			continue;
		}
		else
		{
			pCommand->GetLastPoint(dX, dY);
			if (!bFindMoveTo || rendererpathcommand_MoveTo == pCommand->GetType())
				break;
		}
	}
}
void CPdfRenderer::CPath::GetBounds(double& dL, double& dT, double& dR, double& dB)
{
	GetLastPoint(dL, dT);
	dR = dL;
	dB = dT;

	for (int nIndex = 0, nCount = m_vCommands.size(); nIndex < nCount; nIndex++)
	{
		CPathCommandBase* pCommand = m_vCommands.at(nIndex);
		pCommand->UpdateBounds(dL, dT, dR, dB);
	}
}
void CPdfRenderer::CPath::CPathMoveTo::Draw(PdfWriter::CPage* pPage)
{
	pPage->MoveTo(x, y);
}
void CPdfRenderer::CPath::CPathMoveTo::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	UpdateMaxMinPoints(dL, dT, dR, dB, x, y);
}
void CPdfRenderer::CPath::CPathLineTo::Draw(PdfWriter::CPage* pPage)
{
	pPage->LineTo(x, y);
}
void CPdfRenderer::CPath::CPathLineTo::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	UpdateMaxMinPoints(dL, dT, dR, dB, x, y);
}
void CPdfRenderer::CPath::CPathCurveTo::Draw(PdfWriter::CPage* pPage)
{
	pPage->CurveTo(x1, y1, x2, y2, xe, ye);
}
void CPdfRenderer::CPath::CPathCurveTo::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	UpdateMaxMinPoints(dL, dT, dR, dB, x1, y1);
	UpdateMaxMinPoints(dL, dT, dR, dB, x2, y2);
	UpdateMaxMinPoints(dL, dT, dR, dB, xe, ye);
}
void CPdfRenderer::CPath::CPathArcTo::Draw(PdfWriter::CPage* pPage)
{
	if (sweepAngle >= 360 - 0.001)
		pPage->Ellipse(x + w / 2, y + h / 2, w / 2, h / 2);
	else
		pPage->EllipseArcTo(x + w / 2, y + h / 2, w / 2, h / 2, 360 - startAngle, 360 - (startAngle + sweepAngle), sweepAngle > 0 ? true : false);
}
void CPdfRenderer::CPath::CPathArcTo::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	UpdateMaxMinPoints(dL, dT, dR, dB, x, y);
	UpdateMaxMinPoints(dL, dT, dR, dB, x + w, y + h);
}
void CPdfRenderer::CPath::CPathClose::Draw(PdfWriter::CPage* pPage)
{
	pPage->ClosePath();
}
void CPdfRenderer::CPath::CPathClose::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
}
void CPdfRenderer::CPath::CPathTextChar::Draw(PdfWriter::CPage* pPage)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathTextChar::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathText::Draw(PdfWriter::CPage* pPage)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathText::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathTextExChar::Draw(PdfWriter::CPage* pPage)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathTextExChar::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathTextEx::Draw(PdfWriter::CPage* pPage)
{
	// TODO: �����������
}
void CPdfRenderer::CPath::CPathTextEx::UpdateBounds(double& dL, double& dT, double& dR, double& dB)
{
	// TODO: �����������
}
void CPdfRenderer::CBrushState::Reset()
{
	m_lType = c_BrushTypeSolid;
	m_oColor1.Set(0);
	m_oColor2.Set(0);
	m_nAlpha1 = 255;
	m_nAlpha2 = 255;
	m_wsTexturePath = L"";
	m_lTextureMode  = c_BrushTextureModeStretch;
	m_nTextureAlpha = 255;
	m_dLinearAngle  = 0;
	m_oRect.Reset();

	if (m_pShadingColors)
		delete[] m_pShadingColors;

	if (m_pShadingPoints)
		delete[] m_pShadingPoints;

	m_pShadingColors      = NULL;
	m_pShadingPoints      = NULL;
	m_lShadingPointsCount = 0;
}