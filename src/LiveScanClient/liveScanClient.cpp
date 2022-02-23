//   Copyright (C) 2015  Marek Kowalski (M.Kowalski@ire.pw.edu.pl), Jacek Naruniec (J.Naruniec@ire.pw.edu.pl)
//   License: MIT Software License   See LICENSE.txt for the full license.

//   If you use this software in your research, then please use the following citation:

//    Kowalski, M.; Naruniec, J.; Daniluk, M.: "LiveScan3D: A Fast and Inexpensive 3D Data
//    Acquisition System for Multiple Kinect v2 Sensors". in 3D Vision (3DV), 2015 International Conference on, Lyon, France, 2015

//    @INPROCEEDINGS{Kowalski15,
//        author={Kowalski, M. and Naruniec, J. and Daniluk, M.},
//        booktitle={3D Vision (3DV), 2015 International Conference on},
//        title={LiveScan3D: A Fast and Inexpensive 3D Data Acquisition System for Multiple Kinect v2 Sensors},
//        year={2015},
//    }
#include "stdafx.h"
#include "resource.h"
#include "LiveScanClient.h"
#include "filter.h"
#include <chrono>
#include <strsafe.h>
#include <fstream>
#include "zstd.h"
#include <KinectConfiguration.h>
#include <shellapi.h> // HOGUE
std::mutex m_mSocketThreadMutex;

// HOGUE
int g_winWidth = 800;
int g_winHeight = 540;
int g_winX = 0;
int g_winY = 0;
int g_connectToServerImmediately = 0;


int APIENTRY wWinMain(
	_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR lpCmdLine,
	_In_ int nShowCmd
)
{
	UNREFERENCED_PARAMETER(hPrevInstance);
	//UNREFERENCED_PARAMETER(lpCmdLine);
	//std::cout << lpCmdLine<<std::endl;
	// HOGUE: THIS SHOULD BE DONE IN A MUCH BETTER WAY
	LPWSTR* szArgList;
	int argCount;
	szArgList = CommandLineToArgvW(GetCommandLine(), &argCount);
	if (argCount >= 5) {
		// assume window width, height, x, y
		g_winWidth = _wtoi(szArgList[1]);
		g_winHeight = _wtoi(szArgList[2]);
		g_winX = _wtoi(szArgList[3]);
		g_winY = _wtoi(szArgList[4]);
		if (argCount >= 6) g_connectToServerImmediately = _wtoi(szArgList[5]);
	}

#ifdef _DEBUG
	AllocConsole();
	freopen("CONOUT$", "w", stdout);
	freopen("CONOUT$", "w", stderr);
#endif

	LiveScanClient application;
	application.Run(hInstance, nShowCmd);
}

LiveScanClient::LiveScanClient() :
	m_hWnd(NULL),
	m_nLastCounter(0),
	m_nFramesSinceUpdate(0),
	m_fFreq(0),
	m_nNextStatusTime(0LL),
	m_pD2DFactory(NULL),
	m_pD2DImageRenderer(NULL),
	m_pRainbowColorDepth(NULL),
	m_pCameraSpaceCoordinates(NULL),
	m_pColorInColorSpace(NULL),
	m_pDepthInColorSpace(NULL),
	m_bCalibrate(false),
	m_bCaptureFrame(false),
	m_bRecordingStart(false),
	m_bRecordingStop(false),
	m_bConnected(false),
	m_bConfirmCaptured(false),
	m_bConfirmCalibrated(false),
	m_bShowDepth(false),
	m_bShowPreviewDuringRecording(false),
	m_bSocketThread(true),
	m_bFrameCompression(true),
	m_iCompressionLevel(2),
	m_pClientSocket(NULL),
	m_bRestartingCamera(false),
	m_bRequestConfiguration(false),
	m_bSendConfiguration(false),
	m_bAutoExposureEnabled(true),
	m_nExposureStep(-5),
	m_nExtrinsicsStyle(0), // 0 = no export of extrinsics
	m_eCaptureMode(CM_POINTCLOUD),
	m_nFrameIndex(0)
{
	pCapture = new AzureKinectCapture();

	LARGE_INTEGER qpf = { 0 };
	if (QueryPerformanceFrequency(&qpf))
	{
		m_fFreq = double(qpf.QuadPart);
	}

	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(-0.5);
	m_vBounds.push_back(0.5);
	m_vBounds.push_back(0.5);
	m_vBounds.push_back(0.5);
}

LiveScanClient::~LiveScanClient()
{
	// clean up Direct2D renderer
	if (m_pD2DImageRenderer)
	{
		delete m_pD2DImageRenderer;
		m_pD2DImageRenderer = NULL;
	}

	if (pCapture)
	{
		delete pCapture;
		pCapture = NULL;
	}

	if (m_pRainbowColorDepth)
	{
		delete[] m_pRainbowColorDepth;
		m_pRainbowColorDepth = NULL;
	}

	if (m_pCameraSpaceCoordinates)
	{
		delete[] m_pCameraSpaceCoordinates;
		m_pCameraSpaceCoordinates = NULL;
	}

	if (m_pColorInColorSpace)
	{
		delete[] m_pColorInColorSpace;
		m_pColorInColorSpace = NULL;
	}

	if (m_pDepthInColorSpace)
	{
		delete[] m_pDepthInColorSpace;
		m_pDepthInColorSpace = NULL;
	}

	if (m_pClientSocket)
	{
		delete m_pClientSocket;
		m_pClientSocket = NULL;
	}

	// clean up Direct2D
	SafeRelease(m_pD2DFactory);
}

int LiveScanClient::Run(HINSTANCE hInstance, int nCmdShow)
{
	MSG       msg = { 0 };
	WNDCLASS  wc;

	// Dialog custom window class
	ZeroMemory(&wc, sizeof(wc));
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.cbWndExtra = DLGWINDOWEXTRA;
	wc.hCursor = LoadCursorW(NULL, IDC_ARROW);
	wc.hIcon = LoadIconW(hInstance, MAKEINTRESOURCE(IDI_APP));
	wc.lpfnWndProc = DefDlgProcW;
	wc.lpszClassName = L"LiveScanClientAppDlgWndClass";

	if (!RegisterClassW(&wc))
	{
		return 0;
	}

	// Create main application window
	HWND hWndApp = CreateDialogParamW(
		NULL,
		MAKEINTRESOURCE(IDD_APP),
		NULL,
		(DLGPROC)LiveScanClient::MessageRouter,
		reinterpret_cast<LPARAM>(this));

	// Show window
	ShowWindow(hWndApp, nCmdShow);

	// HOGUE
	::SetWindowPos(m_hWnd, HWND_TOP, g_winX, g_winY, g_winWidth, g_winHeight, NULL);
	std::thread t1(&LiveScanClient::SocketThreadFunction, this);
	// HOGUE
	if (g_connectToServerImmediately) Connect();
	// Main message loop
	while (WM_QUIT != msg.message)
	{
		UpdateFrame();

		while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (WM_QUIT == msg.message)
			{
				break;
			}
			// If a dialog message will be taken care of by the dialog proc
			if (hWndApp && IsDialogMessageW(hWndApp, &msg))
			{
				continue;
			}

			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}
	}

	m_bSocketThread = false;
	t1.join();
	return static_cast<int>(msg.wParam);
}

void LiveScanClient::UpdateFrame()
{
	//I don't think we need the RestartingCamera lock here
	if (!pCapture->bInitialized || m_bRestartingCamera)
	{
		return;
	}

	//Updates hardware settings to the configuration file
	if (m_bRequestConfiguration)
	{
		configuration.eHardwareSyncState = static_cast<SYNC_STATE>(pCapture->GetSyncJackState());
		m_bRequestConfiguration = false;
		m_bSendConfiguration = true;
	}

	//Updates global settings on the device
	if (m_bUpdateSettings)
	{
		pCapture->SetExposureState(m_bAutoExposureEnabled, m_nExposureStep);
		m_bUpdateSettings = false;
	}

	if (m_bRestartCamera)
	{
		if (ReinitAndConfirm())
			m_bRestartCamera = false;
	}

	if (m_bRecordingStart)
	{
		m_nFrameIndex = 0;
		m_vTimestamps.clear();
		m_vFrameNumbers.clear();

		if (!m_bShowPreviewDuringRecording) {
			m_bPreviewDisabled = true;
		}

		m_bRecordingStart = false;
	}

	if (m_bRecordingStop)
	{
		m_framesFileWriterReader.WriteTimestampLog(m_vFrameNumbers, m_vTimestamps, configuration.nGlobalDeviceIndex);
		m_bRecordingStop = false;
		m_bPreviewDisabled = false;
	}

	//Capture logic

	//We always need to capture the raw frame data
	if (pCapture->AquireRawFrame())
	{
		//We lock the network thread so it that the requirement variables don't change while we process the frame 
		std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);

		//To optimize our use of system resources, we only process what is needed
		bool generateRBGData = false;
		bool generateDepthToColorData = false;
		bool generatePointcloud = false;

		if (m_eCaptureMode == CM_POINTCLOUD || m_bCalibrate || m_bRequestLiveFrame) {
			generateRBGData = true;
			generateDepthToColorData = true;
			generatePointcloud = true;
		}

		if (!m_bPreviewDisabled && !m_bShowDepth)
			generateRBGData = true;

		if (!m_bPreviewDisabled && m_bShowDepth)
			generateDepthToColorData = true;

		if (generateRBGData) {
			pCapture->DecodeRawColor();
			pCapture->DownscaleColorImgToDepthImgSize();
		}

		if (generateDepthToColorData)
			pCapture->MapDepthToColor();

		if (generatePointcloud)
			pCapture->GeneratePointcloud();


		if (m_bCaptureFrame)
		{
			uint64_t timeStamp = pCapture->GetTimeStamp();
			m_vFrameNumbers.push_back(m_nFrameIndex);
			m_vTimestamps.push_back(timeStamp);
			m_nFrameIndex++;

			if (m_eCaptureMode == CM_RAW)
			{
				SaveRawFrame();
			}

			else if (m_eCaptureMode == CM_POINTCLOUD)
			{
				StoreFrame(pCapture->pointCloudImage, pCapture->colorBGR);
				SavePointcloudFrame(timeStamp);
			}

			m_bConfirmCaptured = true;
			m_bCaptureFrame = false;
		}

		if (m_bCalibrate) {
			Calibrate();
		}

		if (m_bRequestLiveFrame) {
			StoreFrame(pCapture->pointCloudImage, pCapture->colorBGR);
			SendFrame(m_vLastFrameVertices, m_vLastFrameVerticesSize, m_vLastFrameRGB);
			m_bRequestLiveFrame = false;
		}

		if (!m_bPreviewDisabled)
		{
			if (!m_bShowDepth)
				ShowColor();
			else
				ShowDepth();
		}

		else {
			ShowPreviewDisabled();
		}
	}

	ShowFPS();
}

LRESULT CALLBACK LiveScanClient::MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	LiveScanClient* pThis = NULL;

	if (WM_INITDIALOG == uMsg)
	{
		pThis = reinterpret_cast<LiveScanClient*>(lParam);
		SetWindowLongPtr(hWnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(pThis));
	}
	else
	{
		pThis = reinterpret_cast<LiveScanClient*>(::GetWindowLongPtr(hWnd, GWLP_USERDATA));
	}

	if (pThis)
	{
		return pThis->DlgProc(hWnd, uMsg, wParam, lParam);
	}

	return 0;
}
// HOGUE
void LiveScanClient::Connect() {
	std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);

	if (m_bConnected)
	{
		std::cout << "Disconnecting from server" << std::endl;
		delete m_pClientSocket;
		m_pClientSocket = NULL;

		m_bConnected = false;
		SetDlgItemTextA(m_hWnd, IDC_BUTTON_CONNECT, "Connect");
	}
	else
	{
		try
		{
			std::cout << "Trying to connect to server" << std::endl;
			char address[20];
			GetDlgItemTextA(m_hWnd, IDC_IP, address, 20);
			m_pClientSocket = new SocketClient(address, 48001);

			m_bConnected = true;
			if (calibration.bCalibrated)
				m_bConfirmCalibrated = true;

			SetDlgItemTextA(m_hWnd, IDC_BUTTON_CONNECT, "Disconnect");
			//Clear the status bar so that the "Failed to connect..." disappears.
			SetStatusMessage(L"", 1, true);
		}
		catch (...)
		{
			std::cout << "ERROR: Couldn't connect to server" << std::endl;
			SetStatusMessage(L"Failed to connect. Did you start the server?", 10000, true);
		}
	}
}
LRESULT CALLBACK LiveScanClient::DlgProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(wParam);
	UNREFERENCED_PARAMETER(lParam);

	switch (message)
	{
	case WM_INITDIALOG:
	{
		// Bind application window handle
		m_hWnd = hWnd;

		// Init Direct2D
		D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &m_pD2DFactory);

		// Get and initialize the default Kinect sensor as standalone
		configuration = *new KinectConfiguration();
		configuration.eSoftwareSyncState = Standalone;
		bool res = pCapture->Initialize(configuration);
		if (res)
		{
			std::cout << "Device could be opened successfully" << std::endl;
			configuration.eHardwareSyncState = static_cast<SYNC_STATE>(pCapture->GetSyncJackState());
			calibration.LoadCalibration(pCapture->serialNumber);
			//m_pDepthRGBX = new RGB[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
			m_pDepthInColorSpace = new UINT16[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
			m_pCameraSpaceCoordinates = new Point3f[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
			m_pColorInColorSpace = new RGB[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
			pCapture->SetExposureState(true, 0);
		}
		else
		{
			std::cout << "ERROR: Device failed to open/initialize" << std::endl;
			SetStatusMessage(L"Capture device failed to initialize!", 10000, true);
		}

		ReadIPFromFile();
	}
	break;
	case WM_SIZING: {// HOGUE
		/*	RECT r;
			::GetWindowRect(m_hWnd, &r);
			int w = abs(r.right - r.left);
			int h = abs(r.bottom - r.top);
			::SetWindowPos(m_hWnd, HWND_TOP, 0, 0, w, (w / 1.55) + 200, NULL);*/
	}
	case WM_SIZE: {
		// HOGUE: this "works" but is pretty dumb logic, needs fewer hardcoded things but it serves its purpose
		RECT r;
		::GetWindowRect(m_hWnd, &r);
		int w = abs(r.right - r.left);
		int h = abs(r.bottom - r.top);
		int cw = 90;
		int ch = 12;
		float asp = 1920 / 1080;// pCapture->nColorFrameWidth / pCapture->nColorFrameHeight;
		int h2 = w / asp;
		int startB = 80;
		int fixedHeight = 3 * ch + startB;

		::SetWindowPos(GetDlgItem(m_hWnd, IDC_BUTTON_CONNECT), HWND_TOP, 0, h - (ch / 2) - startB, cw, ch, SWP_NOSIZE);
		::SetWindowPos(GetDlgItem(m_hWnd, IDC_BUTTON_SWITCH), HWND_TOP, 0, h - 2 * ch - (ch / 2) - startB, cw, ch, SWP_NOSIZE);

		::SetWindowPos(GetDlgItem(m_hWnd, IDC_IP), HWND_TOP, cw + cw / 2, h - (ch / 2) - startB, cw, ch, SWP_NOSIZE);
		::SetWindowPos(GetDlgItem(m_hWnd, IDC_STATIC), HWND_TOP, cw + cw / 2, h - 2 * ch - (ch / 2) - startB, cw, ch, SWP_NOSIZE);
		::SetWindowPos(GetDlgItem(m_hWnd, IDC_STATUS), HWND_TOP, 0, h - 60, w, ch * 2, NULL);

		::SetWindowPos(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), HWND_TOP, 0, 0, w, h - fixedHeight, NULL);

		break;
	}
				// If the titlebar X is clicked, destroy app
	case WM_CLOSE:
		pCapture->Close();
		WriteIPToFile();
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		// Quit the main message pump
		PostQuitMessage(0);
		break;

		// Handle button press
	case WM_COMMAND:
		if (IDC_BUTTON_CONNECT == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
		{
			Connect();
		}
		if (IDC_BUTTON_SWITCH == LOWORD(wParam) && BN_CLICKED == HIWORD(wParam))
		{
			m_bShowDepth = !m_bShowDepth;

			if (m_bShowDepth)
			{
				SetDlgItemTextA(m_hWnd, IDC_BUTTON_SWITCH, "Show color");
			}
			else
			{
				SetDlgItemTextA(m_hWnd, IDC_BUTTON_SWITCH, "Show depth");
			}
		}
		break;
	}

	return FALSE;
}

void LiveScanClient::SaveRawFrame()
{
	std::cout << "Saving Raw Frame" << std::endl;
	m_framesFileWriterReader.WriteColorJPGFile(k4a_image_get_buffer(pCapture->colorImageMJPG), k4a_image_get_size(pCapture->colorImageMJPG), m_nFrameIndex);
	m_framesFileWriterReader.WriteDepthTiffFile(pCapture->depthImage16Int, m_nFrameIndex);
}

void LiveScanClient::SavePointcloudFrame(uint64_t timeStamp)
{
	std::cout << "Saving Pointcloud Frame" << std::endl;
	m_framesFileWriterReader.writeFrame(m_vLastFrameVertices, m_vLastFrameVerticesSize, m_vLastFrameRGB, timeStamp, configuration.nGlobalDeviceIndex);
}

void LiveScanClient::Calibrate() {

	std::cout << "Calibrating Client" << std::endl;

	Point3f* pCameraCoordinates = new Point3f[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
	pCapture->PointCloudImageToPoint3f(pCameraCoordinates);

	bool res = calibration.Calibrate(pCapture->colorBGR, pCameraCoordinates, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight);

	delete[] pCameraCoordinates;

	if (res)
	{
		std::cout << "Calibration successfull" << std::endl;

		calibration.SaveCalibration(pCapture->serialNumber);
		m_bConfirmCalibrated = true;
		m_bCalibrate = false;
	}

}

/// <summary>
/// Create and initialize a new Direct2D image renderer (take a look at ImageRenderer.h).
/// We'll use this to draw the data we receive from the Kinect to the screen
/// </summary>
/// <returns></returns>
void LiveScanClient::ManagePreviewWindowInitialization() {

	bool initializationNeeded = false;

	if (m_pD2DImageRenderer != NULL)
	{
		//If our image height/width has changed, we need to reinitialize our D2D Renderer
		if (pCapture->nColorFrameWidth * pCapture->nColorFrameHeight != m_pD2DImageRenderer->GetRenderHeight() * m_pD2DImageRenderer->GetRenderWidth())
			initializationNeeded = true;
	}

	else
		initializationNeeded = true;


	if (initializationNeeded)
	{
		//if there already is a preview D2D Renderer, delete it
		if (m_pD2DImageRenderer)
		{
			delete m_pD2DImageRenderer;
			m_pD2DImageRenderer = NULL;
		}

		if (m_pRainbowColorDepth) {
			delete[] m_pRainbowColorDepth;
			m_pRainbowColorDepth = NULL;
		}

		HRESULT hr;
		m_pD2DImageRenderer = new ImageRenderer();
		hr = m_pD2DImageRenderer->Initialize(GetDlgItem(m_hWnd, IDC_VIDEOVIEW), m_pD2DFactory, pCapture->nColorFrameWidth, pCapture->nColorFrameHeight, pCapture->colorBGR->step);
		if (FAILED(hr))
		{
			std::cout << "ERROR: Failed to initialize the Direct2D draw device" << std::endl;
			SetStatusMessage(L"Failed to initialize the Direct2D draw device.", 10000, true);
		}

		//Initialize Preview Resources
		m_pRainbowColorDepth = new RGB[pCapture->nColorFrameHeight * pCapture->nColorFrameWidth];
		m_cvPreviewDisabled = cv::imread("resources/preview_disabled.png");
		cv::resize(m_cvPreviewDisabled, m_cvPreviewDisabled, cv::Size(pCapture->nColorFrameWidth, pCapture->nColorFrameHeight), cv::INTER_AREA);
		cv::cvtColor(m_cvPreviewDisabled, m_cvPreviewDisabled, cv::COLOR_BGR2BGRA);
	}

}

void LiveScanClient::ShowDepth()
{
	// Make sure we've received valid data
	if (pCapture->transformedDepthImage != NULL)
	{
		ManagePreviewWindowInitialization();

		uint16_t* pointCloudImageData = (uint16_t*)(void*)k4a_image_get_buffer(pCapture->transformedDepthImage);

		for (int i = 0; i < pCapture->nColorFrameWidth * pCapture->nColorFrameHeight; i++)
		{
			BYTE intensity = pointCloudImageData[i] / 40;
			m_pRainbowColorDepth[i].rgbRed = rainbowLookup[intensity][0];
			m_pRainbowColorDepth[i].rgbGreen = rainbowLookup[intensity][1];
			m_pRainbowColorDepth[i].rgbBlue = rainbowLookup[intensity][2];
		}

		// Draw the data with Direct2D
		m_pD2DImageRenderer->Draw(reinterpret_cast<BYTE*>(m_pRainbowColorDepth), pCapture->nColorFrameWidth * pCapture->nColorFrameHeight * sizeof(RGB), pCapture->vBodies);
	}
}

void LiveScanClient::ShowColor()
{
	// Make sure we've received valid data
	if (pCapture->colorBGR)
	{
		ManagePreviewWindowInitialization();

		// Draw the data with Direct2D
		m_pD2DImageRenderer->Draw(reinterpret_cast<BYTE*>(pCapture->colorBGR->data), long(pCapture->colorBGR->total() * pCapture->colorBGR->elemSize()), pCapture->vBodies);
	}
}

void LiveScanClient::ShowPreviewDisabled()
{


	ManagePreviewWindowInitialization();

	m_pD2DImageRenderer->Draw(reinterpret_cast<BYTE*>(m_cvPreviewDisabled.data), long(m_cvPreviewDisabled.total() * m_cvPreviewDisabled.elemSize()), pCapture->vBodies);
	m_bPreviewDisabled = true;
}



bool LiveScanClient::SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce)
{
	INT64 now = GetTickCount64();

	if (m_hWnd && (bForce || (m_nNextStatusTime <= now)))
	{
		SetDlgItemText(m_hWnd, IDC_STATUS, szMessage);
		m_nNextStatusTime = now + nShowTimeMsec;

		return true;
	}

	return false;
}

void LiveScanClient::SocketThreadFunction()
{
	while (m_bSocketThread)
	{
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		HandleSocket();
	}
}

//This is running on a seperate thread!
void LiveScanClient::HandleSocket()
{
	char byteToSend;
	std::lock_guard<std::mutex> lock(m_mSocketThreadMutex);

	if (!m_bConnected)
	{
		return;
	}

	string received = m_pClientSocket->ReceiveBytes();

	for (unsigned int i = 0; i < received.length(); i++)
	{
		std::cout << "Received Server message: " << char(received[i]) << std::endl;

		//capture a frame
		if (received[i] == MSG_CAPTURE_FRAME)
		{
			std::cout << "Capture single frame Received" << std::endl;
			m_bCaptureFrame = true;
		}

		if (received[i] == MSG_RECORDING_START)
		{
			std::cout << "Capture frame start received" << std::endl;
			m_bRecordingStart = true;
		}

		if (received[i] == MSG_RECORDING_STOP)
		{
			std::cout << "Capture frame stop received" << std::endl;
			m_bRecordingStop = true;
		}

		//calibrate
		else if (received[i] == MSG_CALIBRATE)
		{
			std::cout << "Calibrate command recieved" << std::endl;
			m_bCalibrate = true;
		}

		//Restart The Device without changing any settings - must be done after turning on/off temporal sync, and when changing depth mode.
		else if (received[i] == MSG_REINITIALIZE_WITH_CURRENT_SETTINGS)
		{
			std::cout << "Reinitializing device with current settings" << std::endl;
			m_bRestartCamera = true;
		}


		else if (received[i] == MSG_SET_CONFIGURATION)
		{
			std::cout << "Recieved new configuration" << std::endl;

			i++;
		std:string message;
			//TODO: this can be done with substrings, im sure.
			for (int x = 0; x < KinectConfiguration::byteLength; x++)
			{
				message.push_back(received[i + x]);
			}

			i += KinectConfiguration::byteLength;
			configuration.SetFromBytes(message);

			i--;
		}

		//receive settings
		//TODO: what if packet is split?
		else if (received[i] == MSG_RECEIVE_SETTINGS)
		{
			std::cout << "Recieved new settings" << std::endl;


			vector<float> bounds(6);
			i++;
			int nBytes = *(int*)(received.c_str() + i);
			i += sizeof(int);

			for (int j = 0; j < 6; j++)
			{
				bounds[j] = *(float*)(received.c_str() + i);
				i += sizeof(float);
			}

			m_vBounds = bounds;

			int nMarkers = *(int*)(received.c_str() + i);
			i += sizeof(int);

			calibration.markerPoses.resize(nMarkers);

			for (int j = 0; j < nMarkers; j++)
			{
				for (int k = 0; k < 3; k++)
				{
					for (int l = 0; l < 3; l++)
					{
						calibration.markerPoses[j].R[k][l] = *(float*)(received.c_str() + i);
						i += sizeof(float);
					}
				}

				for (int k = 0; k < 3; k++)
				{
					calibration.markerPoses[j].t[k] = *(float*)(received.c_str() + i);
					i += sizeof(float);
				}

				calibration.markerPoses[j].markerId = *(int*)(received.c_str() + i);
				i += sizeof(int);
			}

			m_iCompressionLevel = *(int*)(received.c_str() + i);
			i += sizeof(int);
			if (m_iCompressionLevel > 0)
				m_bFrameCompression = true;
			else
				m_bFrameCompression = false;

			m_bAutoExposureEnabled = (received[i] != 0);
			i++;

			m_nExposureStep = *(int*)(received.c_str() + i);
			i += sizeof(int);

			int exportFormat = *(int*)(received.c_str() + i);
			i += sizeof(int);

			if (exportFormat == 0)
			{
				std::cout << "Export format set to Pointcloud" << std::endl;
				m_eCaptureMode = CM_POINTCLOUD;
			}

			if (exportFormat == 1)
			{
				std::cout << "Export format set to Raw Data" << std::endl;
				m_eCaptureMode = CM_RAW;

			}

			m_nExtrinsicsStyle = *(int*)(received.c_str() + i);
			i += sizeof(int);

			m_bShowPreviewDuringRecording = (received[i] != 0);
			i++;

			m_bUpdateSettings = true;

			//so that we do not lose the next character in the stream
			i--;
		}

		//send configuration
		else if (received[i] == MSG_REQUEST_CONFIGURATION)
		{
			std::cout << "Server requests configuration" << std::endl;
			m_bRequestConfiguration = true;
		}

		//send stored frame
		else if (received[i] == MSG_REQUEST_STORED_FRAME)
		{
			std::cout << "Server requests stored frame" << std::endl;
			byteToSend = MSG_STORED_FRAME;
			m_pClientSocket->SendBytes(&byteToSend, 1);

			Point3s* points = NULL;
			RGB* colors = NULL;
			int pointsSize;

			bool res = m_framesFileWriterReader.readFrame(points, colors, pointsSize);
			if (res == false)
			{
				int size = -1;
				m_pClientSocket->SendBytes((char*)&size, 4);
			}
			else
				SendFrame(points, pointsSize, colors);

			delete[] points;
			delete[] colors;
		}
		//send last frame
		else if (received[i] == MSG_REQUEST_LAST_FRAME)
		{
			std::cout << "Server requests lastest frame" << std::endl;
			byteToSend = MSG_LAST_FRAME;
			m_pClientSocket->SendBytes(&byteToSend, 1);

			m_bRequestLiveFrame = true;
		}

		//receive calibration data
		else if (received[i] == MSG_RECEIVE_CALIBRATION)
		{
			std::cout << "Recieving calibration data" << std::endl;

			i++;
			for (int j = 0; j < 3; j++)
			{
				for (int k = 0; k < 3; k++)
				{
					calibration.worldR[j][k] = *(float*)(received.c_str() + i);
					i += sizeof(float);
				}
			}
			for (int j = 0; j < 3; j++)
			{
				calibration.worldT[j] = *(float*)(received.c_str() + i);
				i += sizeof(float);
			}

			//so that we do not lose the next character in the stream
			i--;
		}
		else if (received[i] == MSG_CLEAR_STORED_FRAMES)
		{
			std::cout << "Recieving Clearing stored frames" << std::endl;
			m_framesFileWriterReader.closeFileIfOpened();
		}

		else if (received[i] == MSG_CREATE_DIR) //Creates a dir on the client. Message also marks the start of the recording
		{
			std::cout << "Creating new take directory" << std::endl;


			i++;
			int stringLength = *(int*)(received.c_str() + i); //Get the length of the following string
			i += sizeof(int);

			std::string dirPath;

			dirPath.assign(received, i, stringLength); //Recieved is already a string, so we just copy the characters out of it			

			//Confirmation message that we have created a valid new directory on this system
			int size = 2;
			char* buffer = new char[size];
			buffer[0] = MSG_CONFIRM_DIR_CREATION;

			if (m_framesFileWriterReader.CreateRecordDirectory(dirPath, configuration.nGlobalDeviceIndex))
			{
				buffer[1] = 1;
				m_pClientSocket->SendBytes(buffer, size);
			}

			else
			{
				//Tell the server that the directory creation has failed, server will abort the recording
				buffer[1] = 0;
				m_pClientSocket->SendBytes(buffer, size);
			}

			//Write the calibration intrinsics into the newly created dir if we record raw frames
			if (configuration.config.color_format != K4A_IMAGE_FORMAT_COLOR_BGRA32)
				m_framesFileWriterReader.WriteCalibrationJSON(configuration.nGlobalDeviceIndex, pCapture->calibrationBuffer, pCapture->nCalibrationSize);

		}
	}

	if (m_bConfirmCaptured)
	{
		std::cout << "Sending capture confirmed" << std::endl;

		byteToSend = MSG_CONFIRM_CAPTURED;
		m_pClientSocket->SendBytes(&byteToSend, 1);
		m_bConfirmCaptured = false;
	}

	if (m_bConfirmCalibrated)
	{
		std::cout << "Sending calibration confirmed" << std::endl;

		int size = (9 + 3) * sizeof(float) + sizeof(int) + 1;
		char* buffer = new char[size];
		buffer[0] = MSG_CONFIRM_CALIBRATED;
		int i = 1;

		memcpy(buffer + i, &calibration.iUsedMarkerId, 1 * sizeof(int));
		i += 1 * sizeof(int);
		memcpy(buffer + i, calibration.worldR[0].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldR[1].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldR[2].data(), 3 * sizeof(float));
		i += 3 * sizeof(float);
		memcpy(buffer + i, calibration.worldT.data(), 3 * sizeof(float));
		i += 3 * sizeof(float);

		m_pClientSocket->SendBytes(buffer, size);
		m_bConfirmCalibrated = false;
	}

	if (m_bSendConfiguration)
	{
		std::cout << "Sending configuration" << std::endl;

		int size = configuration.byteLength + 1;
		char* buffer = new char[size];
		buffer[0] = MSG_CONFIGURATION;
		memcpy(buffer + 1, configuration.ToBytes(), KinectConfiguration::byteLength);
		m_pClientSocket->SendBytes(buffer, size);
		m_bSendConfiguration = false;
	}
}

/// <summary>
/// Reinitialize. Must be called after changing depthMode or afer changing temporal sync mode.
/// </summary>
bool LiveScanClient::ReinitAndConfirm()
{
	std::cout << "Reinitializing camera" << std::endl;

	m_bRestartingCamera = true;

	bool res = false;
	res = pCapture->Close();
	if (!res) {
		SetStatusMessage(L"device failed to close! Please restart Application!", 10000, true);
		SendReinitConfirmation(false);
		m_bRestartingCamera = false;
		return false;
	}

	res = pCapture->Initialize(configuration);
	if (!res) {
		SetStatusMessage(L"device failed to reinitialize! Please restart Application!", 10000, true);
		SendReinitConfirmation(false);
		m_bRestartingCamera = false;
		return false;
	}

	else
	{
		configuration.eHardwareSyncState = static_cast<SYNC_STATE>(pCapture->GetSyncJackState());
		m_pDepthInColorSpace = new UINT16[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
		m_pCameraSpaceCoordinates = new Point3f[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
		m_pColorInColorSpace = new RGB[pCapture->nColorFrameWidth * pCapture->nColorFrameHeight];
	}

	SendReinitConfirmation(true);
	m_bRestartingCamera = false;
	return true;
}

void LiveScanClient::SendReinitConfirmation(bool success)
{
	std::cout << "Sending reinitialization confirmation. Reinitialization successfull: " << success << std::endl;

	int size = 2;
	char* buffer = new char[size];
	buffer[0] = MSG_CONFIRM_RESTART;

	if (success)
		buffer[1] = 0;

	else
		buffer[1] = 1;

	m_pClientSocket->SendBytes(buffer, size);
}

void LiveScanClient::SendFrame(Point3s* vertices, int verticesSize, RGB* RGB)
{
	std::cout << "Sending Frame" << std::endl;

	int size = verticesSize * (3 + 3 * sizeof(short)) + sizeof(int);

	vector<char> buffer(size);
	int pos = 0;

	memcpy(buffer.data() + pos, &verticesSize, sizeof(verticesSize));
	pos += sizeof(verticesSize);

	for (unsigned int i = 0; i < verticesSize; i++)
	{
		buffer[pos++] = RGB[i].rgbRed;
		buffer[pos++] = RGB[i].rgbGreen;
		buffer[pos++] = RGB[i].rgbBlue;

		memcpy(buffer.data() + pos, vertices, sizeof(short) * 3);
		vertices++;
		pos += sizeof(short) * 3;
	}

	int iCompression = static_cast<int>(m_bFrameCompression);

	if (m_bFrameCompression)
	{
		// *2, because according to zstd documentation, increasing the size of the output buffer above a
		// bound should speed up the compression.
		int cBuffSize = ZSTD_compressBound(size) * 2;
		vector<char> compressedBuffer(cBuffSize);
		int cSize = ZSTD_compress(compressedBuffer.data(), cBuffSize, buffer.data(), size, m_iCompressionLevel);
		size = cSize;
		buffer = compressedBuffer;
	}
	char header[8];
	memcpy(header, (char*)&size, sizeof(size));
	memcpy(header + 4, (char*)&iCompression, sizeof(iCompression));

	m_pClientSocket->SendBytes((char*)&header, sizeof(int) * 2);
	m_pClientSocket->SendBytes(buffer.data(), size);
}

void LiveScanClient::StoreFrame(k4a_image_t pointcloudImage, cv::Mat* colorImage)
{
	std::cout << "Storing Pointcloud Frame" << std::endl;

	unsigned int nVertices = pCapture->nColorFrameHeight * pCapture->nColorFrameWidth;

	int16_t* pointCloudImageData = (int16_t*)(void*)k4a_image_get_buffer(pointcloudImage);

	//To save some processing cost, we allocate a full frame size (nVertices) of a Point3f Vector beforehand
	//instead of using push_back for each vertice. Even though we have to copy the vertices into a clean array
	//later and it uses a little bit more RAM, this gives us a nice speed increase for this function, around 25-50%
	Point3f invalidPoint = Point3f(0, 0, 0, true);
	Point3f* allVertices = new Point3f[nVertices];
	int goodVerticesCount = 0;

	for (unsigned int vertexIndex = 0; vertexIndex < nVertices; vertexIndex++)
	{
		//As the resizing function doesn't return a valid RGB-Reserved value which indicates that this pixel is invalid,
		//we cut all vertices under a distance of 0.0001mm, as the invalid vertices always have a Z-Value of 0
		if (pointCloudImageData[3 * vertexIndex + 2] >= 0.0001) // TODO: Needed? && colorInDepth->data[vertexIndex] == 255)
		{
			Point3f temp;

			temp.X = pointCloudImageData[3 * vertexIndex + 0] / 1000.0f;
			temp.Y = pointCloudImageData[3 * vertexIndex + 1] / 1000.0f;
			temp.Z = pointCloudImageData[3 * vertexIndex + 2] / 1000.0f;

			if (calibration.bCalibrated)
			{
				temp.X += calibration.worldT[0];
				temp.Y += calibration.worldT[1];
				temp.Z += calibration.worldT[2];
				temp = RotatePoint(temp, calibration.worldR);

				if (temp.X < m_vBounds[0] || temp.X > m_vBounds[3]
					|| temp.Y < m_vBounds[1] || temp.Y > m_vBounds[4]
					|| temp.Z < m_vBounds[2] || temp.Z > m_vBounds[5])
				{
					allVertices[vertexIndex] = invalidPoint;
					continue;
				}
			}

			allVertices[vertexIndex] = temp;
			goodVerticesCount++;
		}

		else
		{
			allVertices[vertexIndex] = invalidPoint;
		}
	}


	delete[] m_vLastFrameVertices;
	delete[] m_vLastFrameRGB;

	m_vLastFrameVertices = new Point3s[goodVerticesCount];
	m_vLastFrameRGB = new RGB[goodVerticesCount];
	int validVerticesShortCounter = 0;

	uchar* colorValues = colorImage->data;

	//Copy all valid vertices into a clean vector 
	for (unsigned int i = 0; i < nVertices; i++)
	{
		if (!allVertices[i].Invalid)
		{
			RGB color;
			color.rgbRed = colorValues[i * 4];
			color.rgbGreen = colorValues[(i * 4) + 1];
			color.rgbBlue = colorValues[(i * 4) + 2];

			m_vLastFrameVertices[validVerticesShortCounter] = allVertices[i];
			m_vLastFrameRGB[validVerticesShortCounter] = color;
			validVerticesShortCounter++;
		}
	}

	m_vLastFrameVerticesSize = validVerticesShortCounter;

	delete[] allVertices;

	//Real time filtering has a way to high processing cost, should be done in postprocessing (VolNodes)
	/*if (m_bFilter)
		filter(validVertices, validColorPoints, m_nFilterNeighbors, m_fFilterThreshold);*/
}

void LiveScanClient::ShowFPS()
{
	if (m_hWnd)
	{
		double fps = 0.0;

		LARGE_INTEGER qpcNow = { 0 };
		if (m_fFreq)
		{
			if (QueryPerformanceCounter(&qpcNow))
			{
				if (m_nLastCounter)
				{
					m_nFramesSinceUpdate++;
					fps = m_fFreq * m_nFramesSinceUpdate / double(qpcNow.QuadPart - m_nLastCounter);
				}
			}
		}

		WCHAR szStatusMessage[64];
		StringCchPrintf(szStatusMessage, _countof(szStatusMessage), L" FPS = %0.2f", fps);

		if (SetStatusMessage(szStatusMessage, 1000, false))
		{
			m_nLastCounter = qpcNow.QuadPart;
			m_nFramesSinceUpdate = 0;
		}
	}
}

void LiveScanClient::ReadIPFromFile()
{
	std::cout << "Reading IP from File" << std::endl;

	ifstream file;
	file.open("lastIP.txt");
	if (file.is_open())
	{
		char lastUsedIPAddress[20];
		file.getline(lastUsedIPAddress, 20);
		file.close();
		SetDlgItemTextA(m_hWnd, IDC_IP, lastUsedIPAddress);
	}
}

void LiveScanClient::WriteIPToFile()
{
	std::cout << "Writing IP to File" << std::endl;

	ofstream file;
	file.open("lastIP.txt");
	char lastUsedIPAddress[20];
	GetDlgItemTextA(m_hWnd, IDC_IP, lastUsedIPAddress, 20);
	file << lastUsedIPAddress;
	file.close();
}
