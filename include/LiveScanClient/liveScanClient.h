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
#pragma once

#include "resource.h"
#include "ImageRenderer.h"
#include "SocketCS.h"
#include "calibration.h"
//#include "utils.h"
#include "azureKinectCapture.h"
#include "frameFileWriterReader.h"
#include <thread>
#include <mutex>

class LiveScanClient
{
public:
    LiveScanClient();
    ~LiveScanClient();


    static LRESULT CALLBACK MessageRouter(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    LRESULT CALLBACK        DlgProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
    int                     Run(HINSTANCE hInstance, int nCmdShow);
	void					Connect(); // HOGUE
	bool m_bSocketThread;
private:
	Calibration calibration;

	bool m_bCalibrate;
	bool m_bFilter;
	bool m_bStreamOnlyBodies;



	ICapture *pCapture;

	int m_nFilterNeighbors;
	float m_fFilterThreshold;

	bool m_bCaptureFrames;
	bool m_bCaptureSingleFrame;
	bool m_bStartPreRecordingProcess;
	bool m_bConfirmPreRecordingProcess;
	bool m_bStartPostRecordingProcess;
	bool m_bConfirmPostRecordingProcess;
	bool m_bConnected;
	bool m_bConfirmCaptured;
	bool m_bConfirmCalibrated;
	bool m_bCloseCamera;
	bool m_bConfirmCameraClosed;
	bool m_bInitializeCamera;
	bool m_bConfirmCameraInitialized;
	bool m_bCameraError;
	bool m_bUpdateSettings;
	bool m_bRequestConfiguration;
	bool m_bSendConfiguration;
	bool m_bSendTimeStampList;
	bool m_bPostSyncedListReceived;

	bool m_bShowDepth;
	bool m_bFrameCompression;
	int m_iCompressionLevel;

	bool m_bAutoExposureEnabled;
	int m_nExposureStep;
	KinectConfiguration configuration;

	int m_nExtrinsicsStyle;

	FrameFileWriterReader m_framesFileWriterReader;

	SocketClient *m_pClientSocket;
	std::vector<float> m_vBounds;

	std::vector<Point3s> m_vLastFrameVertices;
	std::vector<RGB> m_vLastFrameRGB;
	std::vector<Body> m_vLastFrameBody;

	std::vector<int> m_vFrameCount;
	std::vector<uint64_t> m_vFrameTimestamps;
	std::vector<int> m_vFrameID;
	std::vector<int> m_vPostSyncedFrameID;

	HWND m_hWnd;
    INT64 m_nLastCounter;
    double m_fFreq;
    INT64 m_nNextStatusTime;
    DWORD m_nFramesSinceUpdate;
	int frameRecordCounter;

	int m_nFrameIndex;

	Point3f* m_pCameraSpaceCoordinates;
	RGB* m_pColorInColorSpace;
	UINT16* m_pDepthInColorSpace;

    // Direct2D
    ImageRenderer* m_pDrawColor;
    ID2D1Factory* m_pD2DFactory;


	//Image Resources
	std::vector<uchar>* emptyJPEGBuffer;
	cv::Mat* emptyDepthMat;
	k4a_image_t emptyDepthFrame;
	RGB* m_pDepthRGBX;
	RGB* m_pBlankGreyImage;


	void CreateBlankGrayImage(const int width, const int height);

	void UpdateFrame();
    void ShowColor();
	void ShowDepth();

    bool SetStatusMessage(_In_z_ WCHAR* szMessage, DWORD nShowTimeMsec, bool bForce);

	void HandleSocket();
	bool Reinit();
	bool CloseCamera();
	bool InitializeCamera();
	void SendPostSyncConfirmation(bool success);
	void SendFrame(vector<Point3s> vertices, vector<RGB> RGB, vector<Body> body);
	bool PostSyncPointclouds();
	bool PostSyncRawFrames();

	void SocketThreadFunction();
	void StoreFrame(Point3f *vertices, RGB *colorInDepth, vector<Body> &bodies, BYTE* bodyIndex);
	void ShowFPS();
	void ReadIPFromFile();
	void WriteIPToFile();
};

