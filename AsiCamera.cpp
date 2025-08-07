///////////////////////////////////////////////////////////////////////////////
// FILE:          ASICamera.cpp
// PROJECT:       Micro-Manager 1.4.23
// SUBSYSTEM:     DeviceAdapters
//-----------------------------------------------------------------------------
// DESCRIPTION:   A camera implementation that is backed by the file system
//                Can access stage positions to choose image to display
//
// AUTHOR:        Mikhail Latyshov
//
// COPYRIGHT:     2024 Mikhail Latyshov
// LICENSE:       Licensed under the Apache License, Version 2.0 (the "License");
//                you may not use this file except in compliance with the License.
//                You may obtain a copy of the License at
//                
//                http://www.apache.org/licenses/LICENSE-2.0
//                
//                Unless required by applicable law or agreed to in writing, software
//                distributed under the License is distributed on an "AS IS" BASIS,
//                WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
//                See the License for the specific language governing permissions and
//                limitations under the License.

#include "ASICamera.h"
using namespace std;

const char* cameraName = "ASICamera";

const char* g_StateDeviceName = "MM_EFW";

const char* g_PixelType_RAW8 = "RAW8";
const char* g_PixelType_RAW12 = "RAW12";
const char* g_PixelType_RAW16 = "RAW16";
const char* g_PixelType_Y8 = "Y8";
const char* g_PixelType_RGB24 = "RGB24";
const char* g_PixelType_RGB48 = "RGB48";

const char* g_DeviceIndex = "Selected Device";
const char* g_Keyword_USBTraffic = "USBTraffic";
const char* g_Keyword_USBTraffic_Auto = "USBTraffic Auto";

const char* g_Keyword_IsHeaterOn = "Anti-dew Switch";
const char* g_Keyword_IsCoolerOn = "Cooler Switch";
const char* g_Keyword_TargetTemp = "Target Temperature";
const char* g_Keyword_CoolPowerPerc = "Cooler Power Percentage";
const char* g_Keyword_WB_R = "White Balance Red";
const char* g_Keyword_WB_B = "White Balance Blue";
const char* g_Keyword_AutoWB = "White Balance Auto";

const char* g_Keyword_on = "on";
const char* g_Keyword_off = "off";

const char* g_Keyword_Gamma = "Gamma";
const char* g_Keyword_AutoExp = "Exp Auto";
const char* g_Keyword_AutoGain = "Gain Auto";
const char* g_Keyword_Flip = "Flip";
const char* g_Keyword_HighSpeedMode = "High Speed Mode";
const char* g_Keyword_HardwareBin = "Hardware Bin";
const char* g_Keyword_USBHost = "USB Host";





#include <fstream>
#include <iostream>
#include <cstdarg>
#include <string>
#include <ctime>

void LogToFile(const char* format, ...) {
	std::ofstream logFile;
	logFile.open("ASICamera_mg_log.txt", std::ios_base::app); // Открываем файл для добавления

	if (!logFile.is_open()) return;

	// Получаем текущее время
	std::time_t now = std::time(nullptr);
	logFile << std::asctime(std::localtime(&now)) << " - ";

	// Форматируем строку
	char buffer[256];
	va_list args;
	va_start(args, format);
	vsnprintf(buffer, sizeof(buffer), format, args);
	va_end(args);

	// Записываем строку в файл
	logFile << buffer << std::endl;
	logFile.close();
}

inline static void OutputDbgPrint(const char* strOutPutString, ...)
{
	//return; // лог только для отладки..

	char strBuf[128] = { 0 };
	sprintf(strBuf, "<%s> ", "MM_ASI");
	va_list vlArgs;
	va_start(vlArgs, strOutPutString);
	vsnprintf((char*)(strBuf + strlen(strBuf)), sizeof(strBuf) - strlen(strBuf), strOutPutString, vlArgs);
	va_end(vlArgs);
#ifdef _DEBUG

	#ifdef _WINDOWS
		OutputDebugStringA(strBuf);
	#elif defined _LIN
		printf("%s", strBuf);
	#endif

#endif
	// Добавляем запись в файл
	LogToFile("%s", strBuf);
}

ASICamera::ASICamera() :
	iBin(1),
	initialized_(false),
	iROIWidth(0),
	iROIHeight(0),
	Status(closed),
	ImgType(ASI_IMG_RAW8),
	uc_pImg(0),
	imageCounter_(0),
	thd_(0),
	pControlCaps(0),
	pRGB32(0),
	pRGB64(0),
	b12RAW(false),
	bRGB48(false),
	ImgFlip(ASI_FLIP_NONE)
{
	// call the base class method to set-up default error codes/messages
	InitializeDefaultErrorMessages();
	ASICameraInfo.CameraID = -1;

	// Description property

	int ret = CreateProperty(MM::g_Keyword_Description, "ASICamera Camera Device Adapter", MM::String, true);

	assert(ret == DEVICE_OK);

	iConnectedCamNum = ASIGetNumOfConnectedCameras();


	vector<string> CamIndexValues;
	for (int i = 0; i < iConnectedCamNum; i++)
	{
		ASIGetCameraProperty(&ASICameraInfo, i);
		strcpy(ConnectedCamName[i], ASICameraInfo.Name);	
		CamIndexValues.push_back(ConnectedCamName[i]);
	}

	CPropertyAction* pAct = new CPropertyAction(this, &ASICamera::OnSelectCamIndex);
	if (iConnectedCamNum > 0)
	{
		strcpy(sz_ModelIndex, ConnectedCamName[0]);//Ĭ�ϴ򿪵�һ��camera
		//iCamIndex = 0;
		ASIGetCameraProperty(&ASICameraInfo, 0);
	}
	else
	{
		strcpy(sz_ModelIndex, "no ASI camera connected");
	}
	//	strcpy(sz_ModelIndex, "DropDown");
	ret = CreateProperty(g_DeviceIndex, sz_ModelIndex, MM::String, false, pAct, true); //ѡ������ͷ���
	SetAllowedValues(g_DeviceIndex, CamIndexValues);
	assert(ret == DEVICE_OK);

	strcpy(FlipArr[ASI_FLIP_BOTH], "both");
	strcpy(FlipArr[ASI_FLIP_HORIZ], "horz");
	strcpy(FlipArr[ASI_FLIP_VERT], "vert");
	strcpy(FlipArr[ASI_FLIP_NONE], "none");
	// camera type pre-initialization property

	//  create live video thread
	thd_ = new SequenceThread(this);
}



ASICamera::~ASICamera()
{
	if (Status != closed)
		Shutdown();

	DeleteImgBuf();
	if (thd_)
		delete thd_;
}

int ASICamera::Initialize()
{
	OutputDbgPrint("open camera ID: %d\n", ASICameraInfo.CameraID);
	if (ASICameraInfo.CameraID < 0)
		return DEVICE_NOT_CONNECTED;
	if (ASICameraInfo.CameraID >= 0)
	{
		if (ASIOpenCamera(ASICameraInfo.CameraID) != ASI_SUCCESS)
			return DEVICE_NOT_CONNECTED;

		if (ASIInitCamera(ASICameraInfo.CameraID) != ASI_SUCCESS)
			return DEVICE_NOT_CONNECTED;

		// CameraName

	//	ASIGetCameraProperty(&ASICameraInfo, iCamIndex);

		char* sz_Name = ASICameraInfo.Name;
		int nRet = CreateStringProperty(MM::g_Keyword_CameraName, sz_Name, true);
		assert(nRet == DEVICE_OK);


		iROIWidth = ASICameraInfo.MaxWidth / iBin / 8 * 8;// 2->1, *2
		iROIHeight = ASICameraInfo.MaxHeight / iBin / 2 * 2;//1->2. *0.5

		ASISetROIFormat(ASICameraInfo.CameraID, iROIWidth, iROIHeight, iBin, ImgType);
		iSetWid = iROIWidth;
		iSetHei = iROIHeight;
		iSetBin = iBin;
		iSetX = 0;
		iSetY = 0;

		long lVal;
		ASI_BOOL bAuto;


		lExpMs = GetExposure();

		Status = opened;


		ASIGetNumOfControls(ASICameraInfo.CameraID, &iCtrlNum);
		DeletepControlCaps(ASICameraInfo.CameraID);
		MallocControlCaps(ASICameraInfo.CameraID);
	}

	if (initialized_)
		return DEVICE_OK;

	OutputDbgPrint("Init property\n");
	// set property list
	// -----------------
	vector<string> boolValues;
	boolValues.push_back(g_Keyword_off);
	boolValues.push_back(g_Keyword_on);
	// binning
	CPropertyAction* pAct = new CPropertyAction(this, &ASICamera::OnBinning);
	int ret = CreateProperty(MM::g_Keyword_Binning, "1", MM::Integer, false, pAct);
	assert(ret == DEVICE_OK);

	vector<string> binningValues;

	int i = 0;
	char cBin[2];
	while (ASICameraInfo.SupportedBins[i] > 0)
	{
		sprintf(cBin, "%d", ASICameraInfo.SupportedBins[i]);
		binningValues.push_back(cBin);
		i++;
	}

	ret = SetAllowedValues(MM::g_Keyword_Binning, binningValues);
	assert(ret == DEVICE_OK);

	// pixel type
	pAct = new CPropertyAction(this, &ASICamera::OnPixelType);
	ret = CreateProperty(MM::g_Keyword_PixelType, g_PixelType_Y8, MM::String, false, pAct);
	assert(ret == DEVICE_OK);

	vector<string> pixelTypeValues;
	if (isImgTypeSupported(ASI_IMG_RAW8))
		pixelTypeValues.push_back(g_PixelType_RAW8);
	if (isImgTypeSupported(ASI_IMG_RAW16))
	{
		pixelTypeValues.push_back(g_PixelType_RAW16);
		pixelTypeValues.push_back(g_PixelType_RAW12);
	}
	if (isImgTypeSupported(ASI_IMG_Y8))
		pixelTypeValues.push_back(g_PixelType_Y8);
	if (isImgTypeSupported(ASI_IMG_RGB24))
	{
		pixelTypeValues.push_back(g_PixelType_RGB24);
		pixelTypeValues.push_back(g_PixelType_RGB48);
	}

	ret = SetAllowedValues(MM::g_Keyword_PixelType, pixelTypeValues);
	assert(ret == DEVICE_OK);

	//gain
	int iMin, iMax;

	ASI_CONTROL_CAPS* pOneCtrlCap = GetOneCtrlCap(ASI_GAIN);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnGain);
		ret = CreateProperty(MM::g_Keyword_Gain, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);

		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(MM::g_Keyword_Gain, iMin, iMax);
	}

	//brightness

	pOneCtrlCap = GetOneCtrlCap(ASI_BRIGHTNESS);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnBrightness);
		ret = CreateProperty(MM::g_Keyword_Offset, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(MM::g_Keyword_Offset, iMin, iMax);
	}

	//USBTraffic
	pOneCtrlCap = GetOneCtrlCap(ASI_BANDWIDTHOVERLOAD);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnUSBTraffic);
		ret = CreateProperty(g_Keyword_USBTraffic, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_USBTraffic, iMin, iMax);

		pAct = new CPropertyAction(this, &ASICamera::OnUSB_Auto);
		ret = CreateProperty(g_Keyword_USBTraffic_Auto, g_Keyword_off, MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		ret = SetAllowedValues(g_Keyword_USBTraffic_Auto, boolValues);
		assert(ret == DEVICE_OK);
	}
	//Temperature

	if (GetOneCtrlCap(ASI_TEMPERATURE))
	{

		pAct = new CPropertyAction(this, &ASICamera::OnTemperature);
		ret = CreateProperty(MM::g_Keyword_CCDTemperature, "0", MM::Float, true, pAct);
		assert(ret == DEVICE_OK);

	}

	// white balance red
	if (ASICameraInfo.IsColorCam)
	{
		pOneCtrlCap = GetOneCtrlCap(ASI_WB_R);
		pAct = new CPropertyAction(this, &ASICamera::OnWB_R);
		ret = CreateProperty(g_Keyword_WB_R, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_WB_R, iMin, iMax);

		// white balance blue
		pOneCtrlCap = GetOneCtrlCap(ASI_WB_B);
		pAct = new CPropertyAction(this, &ASICamera::OnWB_B);
		ret = CreateProperty(g_Keyword_WB_B, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_WB_B, iMin, iMax);

		//auto white balance blue
		pAct = new CPropertyAction(this, &ASICamera::OnAutoWB);
		ret = CreateProperty(g_Keyword_AutoWB, g_Keyword_off, MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		ret = SetAllowedValues(g_Keyword_AutoWB, boolValues);
	}

	//cool
	if (ASICameraInfo.IsCoolerCam)
	{
		//Cooler Switch
		pAct = new CPropertyAction(this, &ASICamera::OnCoolerOn);
		ret = CreateProperty(g_Keyword_IsCoolerOn, g_Keyword_off, MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		vector<string> coolerValues;
		coolerValues.push_back(g_Keyword_off);
		coolerValues.push_back(g_Keyword_on);
		ret = SetAllowedValues(g_Keyword_IsCoolerOn, coolerValues);
		assert(ret == DEVICE_OK);

		//Target Temperature
		pOneCtrlCap = GetOneCtrlCap(ASI_TARGET_TEMP);
		pAct = new CPropertyAction(this, &ASICamera::OnTargetTemp);
		ret = CreateProperty(g_Keyword_TargetTemp, "0", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_TargetTemp, iMin, iMax);
		assert(ret == DEVICE_OK);

		//power percentage
		pOneCtrlCap = GetOneCtrlCap(ASI_COOLER_POWER_PERC);
		pAct = new CPropertyAction(this, &ASICamera::OnCoolerPowerPerc);
		ret = CreateProperty(g_Keyword_CoolPowerPerc, "0", MM::Integer, true, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_CoolPowerPerc, iMin, iMax);
		assert(ret == DEVICE_OK);

		//Anti dew
		pAct = new CPropertyAction(this, &ASICamera::OnHeater);
		ret = CreateProperty(g_Keyword_IsHeaterOn, g_Keyword_off, MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		ret = SetAllowedValues(g_Keyword_IsHeaterOn, coolerValues);
		assert(ret == DEVICE_OK);
	}

	//gamma
	pOneCtrlCap = GetOneCtrlCap(ASI_GAMMA);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnGamma);
		ret = CreateProperty(g_Keyword_Gamma, "1", MM::Integer, false, pAct);
		assert(ret == DEVICE_OK);
		iMin = pOneCtrlCap->MinValue;
		iMax = pOneCtrlCap->MaxValue;
		SetPropertyLimits(g_Keyword_Gamma, iMin, iMax);
	}

	//auto exposure
	pOneCtrlCap = GetOneCtrlCap(ASI_EXPOSURE);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnAutoExp);
		ret = CreateProperty(g_Keyword_AutoExp, "1", MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		SetAllowedValues(g_Keyword_AutoExp, boolValues);
	}
	//auto gain
	pOneCtrlCap = GetOneCtrlCap(ASI_GAIN);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnAutoGain);
		ret = CreateProperty(g_Keyword_AutoGain, "1", MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		SetAllowedValues(g_Keyword_AutoGain, boolValues);

	}

	//flip
	pOneCtrlCap = GetOneCtrlCap(ASI_FLIP);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnFlip);
		ret = CreateProperty(g_Keyword_Flip, "1", MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		boolValues.clear();
		for (int i = 0; i < 4; i++)
			boolValues.push_back(FlipArr[i]);

		SetAllowedValues(g_Keyword_Flip, boolValues);

	}

	//high speed mode
	pOneCtrlCap = GetOneCtrlCap(ASI_HIGH_SPEED_MODE);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnHighSpeedMod);
		ret = CreateProperty(g_Keyword_HighSpeedMode, "1", MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		boolValues.clear();
		boolValues.push_back(g_Keyword_off);
		boolValues.push_back(g_Keyword_on);
		SetAllowedValues(g_Keyword_HighSpeedMode, boolValues);

	}

	//hardware bin
	pOneCtrlCap = GetOneCtrlCap(ASI_HARDWARE_BIN);
	if (pOneCtrlCap)
	{
		pAct = new CPropertyAction(this, &ASICamera::OnHardwareBin);
		ret = CreateProperty(g_Keyword_HardwareBin, "1", MM::String, false, pAct);
		assert(ret == DEVICE_OK);
		boolValues.clear();
		boolValues.push_back(g_Keyword_off);
		boolValues.push_back(g_Keyword_on);
		SetAllowedValues(g_Keyword_HardwareBin, boolValues);

	}

	//USB3 host
	char USBHost[16] = { 0 };
	if (ASICameraInfo.IsUSB3Host)
		strcpy(USBHost, "USB3");
	else
		strcpy(USBHost, "USB2");
	ret = CreateProperty(g_Keyword_USBHost, USBHost, MM::String, true);
	assert(ret == DEVICE_OK);


	// synchronize all properties
	// --------------------------
	ret = UpdateStatus();
	if (ret != DEVICE_OK)
		return ret;
	OutputDbgPrint("Init initialized_ true\n");
	initialized_ = true;
	return DEVICE_OK;
}

int ASICamera::Shutdown()
{
	initialized_ = false;
	OutputDbgPrint("Shutdown initialized_ false\n");
	return DEVICE_OK;
}

void ASICamera::GetName(char * name) const
{
	CDeviceUtils::CopyLimitedString(name, cameraName);
}

long ASICamera::GetImageBufferSize() const
{
	 
	OutputDbgPrint("GetImageBufferSize\n");
	return iROIWidth * iROIHeight * iPixBytes;
}

unsigned ASICamera::GetBitDepth() const//��ɫ�ķ�Χ 8bit �� 16bit
{
	if (ImgType == ASI_IMG_RAW16)
	{
		if (b12RAW)
			return 12;
		else
			return 16;
	}
	else
	{
		if (ImgType == ASI_IMG_RGB24 && bRGB48)
			return 16;
		else
			return 8;
	}
}

int ASICamera::GetBinning() const
{
	return 1;
}

int ASICamera::SetBinning(int)
{
	return DEVICE_OK;
}

void ASICamera::SetExposure(double exp)
{
	lExpMs = exp;
	ASISetControlValue(ASICameraInfo.CameraID, ASI_EXPOSURE, exp * 1000, ASI_FALSE);
}

double ASICamera::GetExposure() const
{
	//	return lExpMs;
	long lVal;
	ASI_BOOL bAuto;
	ASIGetControlValue(ASICameraInfo.CameraID, ASI_EXPOSURE, &lVal, &bAuto);
	return lVal / 1000;

}


/**
	  * Returns the name for each component
	  */
int ASICamera::GetComponentName(unsigned component, char* name)
{
	if (iComponents != 1)
	{
		switch (component)
		{
		case 1:
			strcpy(name, "red");
			break;
		case 2:
			strcpy(name, "green");
			break;
		case 3:
			strcpy(name, "blue");
			break;
		case 4:
			strcpy(name, "0");
			break;
		default:
			strcpy(name, "error");
			break;
		}
	}
	else
		strcpy(name, "grey");
	return DEVICE_OK;
}

/**
* Sets the camera Region Of Interest.
* Required by the MM::Camera API.
* This command will change the dimensions of the image.
* Depending on the hardware capabilities the camera may not be able to configure the
* exact dimensions requested - but should try do as close as possible.
* If the hardware does not have this capability the software should simulate the ROI by
* appropriately cropping each frame.
* This demo implementation ignores the position coordinates and just crops the buffer.
* @param x - top-left corner coordinate
* @param y - top-left corner coordinate
* @param xSize - width
* @param ySize - height
*/
void ASICamera::DeleteImgBuf()
{
	if (uc_pImg)
	{
		delete[] uc_pImg;
		uc_pImg = 0;
		iBufSize = 0;
		OutputDbgPrint("clr\n");
	}
	if (pRGB32)
	{
		delete[] pRGB32;
		pRGB32 = 0;
	}
	if (pRGB64)
	{
		delete[] pRGB64;
		pRGB64 = 0;
	}
}

int ASICamera::SetROI(unsigned x, unsigned y, unsigned xSize, unsigned ySize)
{
	if (xSize == 0 && ySize == 0)
		;
	else
	{
		/*20160107
		����ROI������ʾͼƬΪ���յ�,���򴫽�������ʼ��(x, y)��������ʾͼƬ����ʼ��(����GetROI()�õ�)�������ѡ���ƫ�ƣ�
		�ߴ����ʼ�㶼��ImgBin/iSetBin����, iSetBin��Ҫ���õ�binֵ
		����з�ת, ��Ҫ������������������
		*/
		switch (ImgFlip)
		{
		case ASI_FLIP_NONE:
			break;
		case ASI_FLIP_HORIZ:
			x = ASICameraInfo.MaxWidth / ImgBin - x - xSize;
			break;
		case ASI_FLIP_VERT:
			y = ASICameraInfo.MaxHeight / ImgBin - y - ySize;
			break;
		case ASI_FLIP_BOTH:
			x = ASICameraInfo.MaxWidth / ImgBin - x - xSize;
			y = ASICameraInfo.MaxHeight / ImgBin - y - ySize;
			break;
		}

		iSetWid = xSize * ImgBin / iSetBin;// 2->1, *2
		iSetHei = ySize * ImgBin / iSetBin;//1->2. *0.5
		iSetWid = iSetWid / 8 * 8;
		iSetHei = iSetHei / 2 * 2;


		iSetX = x * ImgBin / iSetBin;//bin�ı��, startpos�������bin��Ļ���ģ�ҲҪ���ձ����ı�
		iSetY = y * ImgBin / iSetBin;
		iSetX = iSetX / 4 * 4;
		iSetY = iSetY / 2 * 2;

		if (ASISetROIFormat(ASICameraInfo.CameraID, iSetWid, iSetHei, iSetBin, ImgType) == ASI_SUCCESS)//������óɹ�
		{
			OutputDbgPrint("wid:%d hei:%d bin:%d\n", xSize, ySize, iBin);
			DeleteImgBuf();//buff��С�ı�
			ASISetStartPos(ASICameraInfo.CameraID, iSetX, iSetY);
		}
		ASIGetROIFormat(ASICameraInfo.CameraID, &iROIWidth, &iROIHeight, &iBin, &ImgType);
	}
	return DEVICE_OK;
}

int ASICamera::GetROI(unsigned & x, unsigned & y, unsigned & xSize, unsigned & ySize)
{
	/* 20160107
	�õ���ʾͼ���ROI��Ϣ
		����з�ת, Ҫ����ɷ�����ߵ�����, ���������ӵõ���ROI,�ٻ�����������������*/

	x = ImgStartX;
	y = ImgStartY;
	switch (ImgFlip)
	{
	case ASI_FLIP_NONE:
		break;
	case ASI_FLIP_HORIZ:
		x = ASICameraInfo.MaxWidth / ImgBin - ImgStartX - ImgWid;
		break;
	case ASI_FLIP_VERT:
		y = ASICameraInfo.MaxHeight / ImgBin - ImgStartY - ImgHei;
		break;
	case ASI_FLIP_BOTH:
		x = ASICameraInfo.MaxWidth / ImgBin - ImgStartX - ImgWid;
		y = ASICameraInfo.MaxHeight / ImgBin - ImgStartY - ImgHei;
		break;
	}
	xSize = ImgWid;
	ySize = ImgHei;
	return DEVICE_OK;
}

int ASICamera::ClearROI()
{
	//  ResizeImageBuffer();
	iSetWid = iROIWidth = ASICameraInfo.MaxWidth / iBin / 8 * 8;
	iSetHei = iROIHeight = ASICameraInfo.MaxHeight / iBin / 2 * 2;

	if (ASISetROIFormat(ASICameraInfo.CameraID, iROIWidth, iROIHeight, iBin, ImgType) == ASI_SUCCESS)
	{
		ASISetStartPos(ASICameraInfo.CameraID, 0, 0);
		iSetX = iSetY = 0;
		DeleteImgBuf();
	}
	return DEVICE_OK;
}

int ASICamera::IsExposureSequenceable(bool & isSequenceable) const
{
	isSequenceable = false;

	return DEVICE_OK;
}



/*
* Inserts Image and MetaData into MMCore circular Buffer
*/
int ASICamera::InsertImage()
{
	//OutputDbgPrint("InsertImage\n");
	MM::MMTime timeStamp = this->GetCurrentMMTime();
	char label[MM::MaxStrLength];
	this->GetLabel(label);

	// Important:  metadata about the image are generated here:
	Metadata md;
	md.put("Camera", label);

	char buf[MM::MaxStrLength];
	GetProperty(MM::g_Keyword_Binning, buf);
	md.put(MM::g_Keyword_Binning, buf);

	//   MMThreadGuard g(imgPixelsLock_);

	const unsigned char* pI;
	pI = GetImageBuffer();
	int ret = 0;
	ret = GetCoreCallback()->InsertImage(this, pI, iROIWidth, iROIHeight, iPixBytes, md.Serialize().c_str());
	if (ret == DEVICE_BUFFER_OVERFLOW)//����������Ҫ���, �����ܼ�������ͼ�����ס
	{
		// do not stop on overflow - just reset the buffer
		GetCoreCallback()->ClearImageBuffer(this);
		// don't process this same image again...
		return GetCoreCallback()->InsertImage(this, pI, iROIWidth, iROIHeight, iPixBytes, md.Serialize().c_str(), false);
	}
	else
		return ret;
}


unsigned ASICamera::GetImageWidth() const
{
	 

	return iROIWidth;
}

unsigned ASICamera::GetImageHeight() const
{
	 

	return iROIHeight;
}

unsigned ASICamera::GetImageBytesPerPixel() const
{
	return iPixBytes;
}

int ASICamera::SnapImage()
{
	//  GenerateImage();
//	ASIGetStartPos(iCamIndex, &iStartXImg, &iStartYImg);
	ASIStartExposure(ASICameraInfo.CameraID, ASI_FALSE);
	Status = snaping;
	unsigned long time = GetTickCount(), deltaTime = 0;
	ASI_EXPOSURE_STATUS exp_status;
	do
	{
		ASIGetExpStatus(ASICameraInfo.CameraID, &exp_status);
		//OutputDbgPrint("SnapImage do exp_status %d\n", (int)exp_status);
		deltaTime = GetTickCount() - time;
		if (deltaTime > 10000 && GetTickCount() - time > 3 * lExpMs)
		{
			OutputDbgPrint("SnapImage delta %d ms, stop snap\n", deltaTime);
			ASIStopExposure(ASICameraInfo.CameraID);
			break;
		}

		Sleep(1);
	} while (exp_status == ASI_EXP_WORKING);

	Status = opened;

	if (uc_pImg == 0)
	{
		iBufSize = GetImageBufferSize();
		uc_pImg = new unsigned char[iBufSize];
	}
	if (exp_status == ASI_EXP_SUCCESS)
	{
		OutputDbgPrint("ASI_EXP_SUCCESS exp_status %d\n", (int)exp_status);
		ASIGetDataAfterExp(ASICameraInfo.CameraID, uc_pImg, iBufSize);
		ASI_BOOL bAuto;
		long lVal;
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_FLIP, &lVal, &bAuto);
		ImgFlip = (ASI_FLIP_STATUS)lVal;
		ASI_IMG_TYPE imgType;
		ASIGetROIFormat(ASICameraInfo.CameraID, &ImgWid, &ImgHei, &ImgBin, &imgType);
		ASIGetStartPos(ASICameraInfo.CameraID, &ImgStartX, &ImgStartY);
	}

	OutputDbgPrint("exp_status %d\n", (int)exp_status);
	if (exp_status == ASI_EXP_SUCCESS)
		return DEVICE_OK;
	else
		return DEVICE_SNAP_IMAGE_FAILED;
}


int ASICamera::PrepareSequenceAcqusition()
{
	if (IsCapturing())
		return DEVICE_CAMERA_BUSY_ACQUIRING;
	/*   int ret = GetCoreCallback()->PrepareForAcq(this);
	if (ret != DEVICE_OK)
	return ret;*/
	return DEVICE_OK;
}


/*
* Do actual capturing
* Called from inside the thread
*/
int ASICamera::RunSequenceOnThread(MM::MMTime startTime)
{
	int ret = DEVICE_ERR;

	if (ASIGetVideoData(ASICameraInfo.CameraID, uc_pImg, iBufSize, 2 * lExpMs) == ASI_SUCCESS)
	{
		ret = InsertImage();
		ASI_BOOL bAuto;
		long lVal;
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_FLIP, &lVal, &bAuto);
		ImgFlip = (ASI_FLIP_STATUS)lVal;

		ASI_IMG_TYPE imgType;
		ASIGetROIFormat(ASICameraInfo.CameraID, &ImgWid, &ImgHei, &ImgBin, &imgType);
		ASIGetStartPos(ASICameraInfo.CameraID, &ImgStartX, &ImgStartY);
	}
	return ret;
}



/**
* Required by the MM::Camera API
* Please implement this yourself and do not rely on the base class implementation
* The Base class implementation is deprecated and will be removed shortly
*/
int ASICamera::StartSequenceAcquisition(double interval) {
	return StartSequenceAcquisition(LONG_MAX, interval, false);
}

/**
* Simple implementation of Sequence Acquisition
* A sequence acquisition should run on its own thread and transport new images
* coming of the camera into the MMCore circular buffer.
*/
int ASICamera::StartSequenceAcquisition(long numImages, double interval_ms, bool stopOnOverflow)
{
	if (IsCapturing())
		return DEVICE_CAMERA_BUSY_ACQUIRING;

	OutputDbgPrint("StartCap\n");

	ASIStartVideoCapture(ASICameraInfo.CameraID);
	Status = capturing;

	OutputDbgPrint("StartSeqAcq\n");
	thd_->Start(numImages, interval_ms);//��ʼ�߳�

	return DEVICE_OK;
}

/**
* Stop and wait for the Sequence thread finished
*/
int ASICamera::StopSequenceAcquisition()
{
	if (!thd_->IsStopped())
	{
		thd_->Stop();//ֹͣ�߳�
		OutputDbgPrint("StopSeqAcq bf wait\n");
		//		if(!thd_->IsStopped())
		thd_->wait();//�ȴ��߳��˳�
		OutputDbgPrint("StopSeqAcq af wait\n");
	}
	//	if(Status == capturing)
	//	{
	ASIStopVideoCapture(ASICameraInfo.CameraID);
	Status = opened;
	//	}

	return DEVICE_OK;
}



void ASICamera::Conv16RAWTo12RAW()
{
	unsigned long line0;
	//	unsigned int *pBuf16 = (unsigned int*)uc_pImg;
#ifdef _WINDOWS
	UINT16* pBuf16 = (UINT16*)uc_pImg;//unsigned short
#else
	uint16_t* pBuf16 = (uint16_t*)uc_pImg;//unsigned short
#endif

	for (int y = 0; y < iROIHeight; y++)
	{
		line0 = iROIWidth * y;
		for (int x = 0; x < iROIWidth; x++)
		{
			pBuf16[line0 + x] /= 16;
		}
	}
}
void ASICamera::ConvRGB2RGBA32()
{
	if (!pRGB32)
	{
		pRGB32 = new unsigned char[iROIWidth * iROIHeight * 4];
	}
	unsigned long index32, index24, line0;
	for (int y = 0; y < iROIHeight; y++)
	{
		line0 = iROIWidth * y;
		for (int x = 0; x < iROIWidth; x++)
		{
			index32 = (line0 + x) * 4;
			index24 = (line0 + x) * 3;
			pRGB32[index32 + 0] = uc_pImg[index24 + 0];
			pRGB32[index32 + 1] = uc_pImg[index24 + 1];
			pRGB32[index32 + 2] = uc_pImg[index24 + 2];
			pRGB32[index32 + 3] = 0;
		}
	}
}

void ASICamera::ConvRGB2RGBA64()
{
	if (!pRGB64)
	{
		pRGB64 = new unsigned char[iROIWidth * iROIHeight * 4 * 2];
		memset(pRGB64, 0, iROIWidth * iROIHeight * 4 * 2);
	}
	unsigned long index64, index24, line0;
	for (int y = 0; y < iROIHeight; y++)
	{
		line0 = iROIWidth * y;
		for (int x = 0; x < iROIWidth; x++)
		{
			index64 = (line0 + x) * 8;
			index24 = (line0 + x) * 3;
			pRGB64[index64 + 1] = uc_pImg[index24 + 0];
			pRGB64[index64 + 3] = uc_pImg[index24 + 1];
			pRGB64[index64 + 5] = uc_pImg[index24 + 2];
			//			pRGB64[index64 + 6] = 0;
		}
	}
}
/**
* Returns pixel data.
* Required by the MM::Camera API.
* The calling program will assume the size of the buffer based on the values
* obtained from GetImageBufferSize(), which in turn should be consistent with
* values returned by GetImageWidth(), GetImageHight() and GetImageBytesPerPixel().
* The calling program allso assumes that camera never changes the size of
* the pixel buffer on its own. In other words, the buffer can change only if
* appropriate properties are set (such as binning, pixel type, etc.)
*/
const unsigned char* ASICamera::GetImageBuffer()
{
	//  return const_cast<unsigned char*>(img_.GetPixels());
	if (ImgType == ASI_IMG_RGB24)
	{
		if (bRGB48)
		{
			ConvRGB2RGBA64();
			return pRGB64;
		}
		else
		{
			ConvRGB2RGBA32();
			return pRGB32;
		}

	}
	else if (ImgType == ASI_IMG_RAW16 && b12RAW)
		Conv16RAWTo12RAW();
	return uc_pImg;
}




bool ASICamera::IsCapturing()
{
	OutputDbgPrint("IsCapturing\n");
	//  return !thd_->IsStopped();
	if (Status == capturing || Status == snaping)
		return true;
	else
		return false;
}



///////////////////////////////////////////////////////////////////////////////
// ASICamera Action handlers
///////////////////////////////////////////////////////////////////////////////

/**
* Handles "Binning" property.
*/
int ASICamera::OnBinning(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::AfterSet)
	{
		long binSize;
		pProp->Get(binSize);
		char binF;
		binF = binSize;

		if (!thd_->IsStopped())//micro manager�������binʱ�����ɳ���ֹͣ�����ã�����property������bin����ֹͣ�����´������Բ�ֹͣʱ��������
			return DEVICE_CAMERA_BUSY_ACQUIRING;
		/* bin��� ��ʼ��ͳߴ��� ������ֵ���� old Bin/new Bin ���ŵ�*/
		iSetWid = iSetWid * iSetBin / binF;// 2->1, *2
		iSetHei = iSetHei * iSetBin / binF;//1->2. *0.5
		iSetWid = iSetWid / 8 * 8;
		iSetHei = iSetHei / 2 * 2;

		iSetX = iSetX * iSetBin / binF;//bin�ı��, startpos�������bin��Ļ���ģ�ҲҪ���ձ����ı�
		iSetY = iSetY * iSetBin / binF;

		if (ASISetROIFormat(ASICameraInfo.CameraID, iSetWid, iSetHei, binF, ImgType) == ASI_SUCCESS)
		{
			DeleteImgBuf();
			ASISetStartPos(ASICameraInfo.CameraID, iSetX, iSetY);//�����¼���startx ��starty������ѡ����ͬ�����Ҫ��������
		}
		ASIGetROIFormat(ASICameraInfo.CameraID, &iROIWidth, &iROIHeight, &iBin, &ImgType);
		iSetBin = binF;
	}
	else if (eAct == MM::BeforeGet)
	{
		ASIGetROIFormat(ASICameraInfo.CameraID, &iROIWidth, &iROIHeight, &iBin, &ImgType);
		pProp->Set((long)iBin);
	}

	return DEVICE_OK;
}

/**
* Handles "PixelType" property.
*/
int ASICamera::OnPixelType(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		string val;
		pProp->Get(val);
		if (Status == capturing)
			return DEVICE_CAMERA_BUSY_ACQUIRING;
		if (val.compare(g_PixelType_RAW8) == 0)
			ImgType = ASI_IMG_RAW8;
		else if (val.compare(g_PixelType_RAW16) == 0)
		{
			ImgType = ASI_IMG_RAW16;
			b12RAW = false;
		}
		else if (val.compare(g_PixelType_RAW12) == 0)
		{
			ImgType = ASI_IMG_RAW16;
			b12RAW = true;
		}
		else if (val.compare(g_PixelType_Y8) == 0)
			ImgType = ASI_IMG_Y8;
		else if (val.compare(g_PixelType_RGB24) == 0)
		{
			ImgType = ASI_IMG_RGB24;
			bRGB48 = false;
		}
		else if (val.compare(g_PixelType_RGB48) == 0)
		{
			ImgType = ASI_IMG_RGB24;
			bRGB48 = true;
		}
		RefreshImgType();
		OutputDbgPrint("w%d h%d b%d t%d\n", iROIWidth, iROIHeight, iBin, ImgType);
		int iStartX, iStartY;
		ASIGetStartPos(ASICameraInfo.CameraID, &iStartX, &iStartY);
		if (ASISetROIFormat(ASICameraInfo.CameraID, iROIWidth, iROIHeight, iBin, ImgType) == ASI_SUCCESS)
		{
			ASISetStartPos(ASICameraInfo.CameraID, iStartX, iStartY);
			DeleteImgBuf();
		}


	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetROIFormat(ASICameraInfo.CameraID, &iROIWidth, &iROIHeight, &iBin, &ImgType);

		if (ImgType == ASI_IMG_RAW8)
			pProp->Set(g_PixelType_RAW8);
		else if (ImgType == ASI_IMG_RAW16)
		{
			if (b12RAW)
				pProp->Set(g_PixelType_RAW12);
			else
				pProp->Set(g_PixelType_RAW16);
		}
		else if (ImgType == ASI_IMG_Y8)
			pProp->Set(g_PixelType_Y8);
		else if (ImgType == ASI_IMG_RGB24)
		{
			if (bRGB48)
				pProp->Set(g_PixelType_RGB48);
			else
				pProp->Set(g_PixelType_RGB24);
		}
		else
			assert(false); // this should never happen

		RefreshImgType();

	}

	return DEVICE_OK;
}

/**
* Handles "Gain" property.
*/
int ASICamera::OnGain(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	OutputDbgPrint("OnGain\n");
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		pProp->Get(lVal);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_GAIN, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_GAIN, &lVal, &bAuto);
		pProp->Set(lVal);
	}

	return DEVICE_OK;
}

/**
* Handles "CamIndex" property.
*/
int ASICamera::OnSelectCamIndex(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	string str;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		pProp->Get(str);
		for (int i = 0; i < iConnectedCamNum; i++)
		{
			if (!str.compare(ConnectedCamName[i]))
			{
				//	iCamIndex = i;
				ASIGetCameraProperty(&ASICameraInfo, i);
				strcpy(sz_ModelIndex, ConnectedCamName[i]);
				break;
			}
		}
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		pProp->Set(sz_ModelIndex);
	}

	return DEVICE_OK;
}
/**
* Handles "Temperature" property.
*/
int ASICamera::OnTemperature(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	OutputDbgPrint("OnTemperature\n");
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		pProp->Get(lVal);
	}
	else if (eAct == MM::BeforeGet)
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_TEMPERATURE, &lVal, &bAuto);
		pProp->Set((double)lVal / 10);
	}


	return DEVICE_OK;
}
/**
* Handles "Brightness" property.
*/
int ASICamera::OnBrightness(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		pProp->Get(lVal);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_BRIGHTNESS, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_BRIGHTNESS, &lVal, &bAuto);
		pProp->Set(lVal);
	}

	return DEVICE_OK;
}

/**
* Handles "USBTraffic" property.
*/
int ASICamera::OnUSBTraffic(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		pProp->Get(lVal);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_BANDWIDTHOVERLOAD, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_BANDWIDTHOVERLOAD, &lVal, &bAuto);
		pProp->Set(lVal);
	}

	return DEVICE_OK;
}

/**
* Handles "USBTraffic Auto" property.
*/
int ASICamera::OnUSB_Auto(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_BANDWIDTHOVERLOAD, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);
		bAuto = strVal.compare(g_Keyword_on) ? ASI_FALSE : ASI_TRUE;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_BANDWIDTHOVERLOAD, lVal, bAuto);
		//		SetPropertyReadOnly(g_Keyword_USBTraffic, bAuto);
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_BANDWIDTHOVERLOAD, &lVal, &bAuto);
		pProp->Set(bAuto == ASI_TRUE ? g_Keyword_on : g_Keyword_off);
		//		SetPropertyReadOnly(g_Keyword_USBTraffic,bAuto);
	}

	return DEVICE_OK;
}
/**
* Handles "Cooler switch" property.
*/
int ASICamera::OnCoolerOn(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	OutputDbgPrint("OnCoolerOn\n");
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		//	ASIGetControlValue(iCamIndex, ASI_TARGET_TEMP, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);//�ӿؼ��õ�ѡ����ֵ
		lVal = !strVal.compare(g_Keyword_on);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_COOLER_ON, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_COOLER_ON, &lVal, &bAuto);
		pProp->Set(lVal > 0 ? g_Keyword_on : g_Keyword_off);
	}
	return DEVICE_OK;
}
/**
* Handles "Heater switch" property.
*/
int ASICamera::OnHeater(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		//	ASIGetControlValue(iCamIndex, ASI_TARGET_TEMP, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);//�ӿؼ��õ�ѡ����ֵ
		lVal = !strVal.compare(g_Keyword_on);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_ANTI_DEW_HEATER, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_ANTI_DEW_HEATER, &lVal, &bAuto);
		pProp->Set(lVal > 0 ? g_Keyword_on : g_Keyword_off);
	}
	return DEVICE_OK;
}
/**
* Handles "Target Temperature" property.
*/
int ASICamera::OnTargetTemp(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_TARGET_TEMP, &lVal, &bAuto);
		pProp->Get(lVal);//�ӿؼ��õ�ѡ����ֵ->����
		ASISetControlValue(ASICameraInfo.CameraID, ASI_TARGET_TEMP, lVal, bAuto);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_TARGET_TEMP, &lVal, &bAuto);
		pProp->Set(lVal);
	}
	return DEVICE_OK;
}
/**
* Handles "power percentage" property.
*/
int ASICamera::OnCoolerPowerPerc(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		pProp->Get(lVal);//�ӿؼ��õ�ѡ����ֵ->����

	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_COOLER_POWER_PERC, &lVal, &bAuto);
		pProp->Set(lVal);
	}
	return DEVICE_OK;
}
/**
* Handles "white balance red" property.
*/
int ASICamera::OnWB_R(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		pProp->Get(lVal);//�ӿؼ��õ�ѡ����ֵ->����
		ASISetControlValue(ASICameraInfo.CameraID, ASI_WB_R, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_WB_R, &lVal, &bAuto);
		pProp->Set(lVal);
	}
	return DEVICE_OK;
}
/**
* Handles "white balance blue" property.
*/
int ASICamera::OnWB_B(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		pProp->Get(lVal);//�ӿؼ��õ�ѡ����ֵ->����
		ASISetControlValue(ASICameraInfo.CameraID, ASI_WB_B, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_WB_B, &lVal, &bAuto);
		pProp->Set(lVal);
	}
	return DEVICE_OK;
}
/**
* Handles "auto white balance" property.
*/
int ASICamera::OnAutoWB(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	string strVal;
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_WB_B, &lVal, &bAuto);
		pProp->Get(strVal);//�ӿؼ��õ�ѡ����ֵ->����
		bAuto = strVal.compare(g_Keyword_on) ? ASI_FALSE : ASI_TRUE;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_WB_B, lVal, bAuto);
		//	SetPropertyReadOnly(g_Keyword_WB_R,bAuto );
		//	SetPropertyReadOnly(g_Keyword_WB_B,bAuto );

	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_WB_B, &lVal, &bAuto);
		pProp->Set(bAuto ? g_Keyword_on : g_Keyword_off);
		//		SetPropertyReadOnly(g_Keyword_WB_R,bAuto );
		//		SetPropertyReadOnly(g_Keyword_WB_B,bAuto );
	}
	return DEVICE_OK;
}
/**
* Handles "auto white balance" property.
*/
int ASICamera::OnGamma(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{

		pProp->Get(lVal);
		ASISetControlValue(ASICameraInfo.CameraID, ASI_GAMMA, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_GAMMA, &lVal, &bAuto);
		pProp->Set(lVal);
	}
	return DEVICE_OK;
}
/**
* Handles "auto exposure" property.
*/
int ASICamera::OnAutoExp(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_EXPOSURE, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);
		bAuto = strVal.compare(g_Keyword_on) ? ASI_FALSE : ASI_TRUE;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_EXPOSURE, lVal, bAuto);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_EXPOSURE, &lVal, &bAuto);
		pProp->Set(bAuto ? g_Keyword_on : g_Keyword_off);
		//		SetPropertyReadOnly(MM::g_Keyword_Exposure,bAuto );
	}
	return DEVICE_OK;
}
/**
* Handles "auto gain" property.
*/
int ASICamera::OnAutoGain(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_GAIN, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);
		bAuto = strVal.compare(g_Keyword_on) ? ASI_FALSE : ASI_TRUE;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_GAIN, lVal, bAuto);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_GAIN, &lVal, &bAuto);
		pProp->Set(bAuto ? g_Keyword_on : g_Keyword_off);
		//	SetPropertyReadOnly(MM::g_Keyword_Gain,bAuto );
	}
	return DEVICE_OK;
}
/**
* Handles "flip" property.
*/
int ASICamera::OnFlip(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_FLIP, &lVal, &bAuto);
		string strVal;
		pProp->Get(strVal);
		for (int i = 0; i < 4; i++)
		{
			if (!strVal.compare(FlipArr[i]))
			{
				ASISetControlValue(ASICameraInfo.CameraID, ASI_FLIP, i, ASI_FALSE);
				break;
			}
		}

	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_FLIP, &lVal, &bAuto);
		pProp->Set(FlipArr[lVal]);
	}
	return DEVICE_OK;
}
/**
* Handles "hight speed mode" property.
*/
int ASICamera::OnHighSpeedMod(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		string strVal;
		pProp->Get(strVal);
		lVal = strVal.compare(g_Keyword_on) ? 0 : 1;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_HIGH_SPEED_MODE, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_HIGH_SPEED_MODE, &lVal, &bAuto);
		pProp->Set(lVal ? g_Keyword_on : g_Keyword_off);
	}
	return DEVICE_OK;
}
/**
* Handles "hardware bin" property.
*/
int ASICamera::OnHardwareBin(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	long lVal;
	ASI_BOOL bAuto;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		string strVal;
		pProp->Get(strVal);
		lVal = strVal.compare(g_Keyword_on) ? 0 : 1;
		ASISetControlValue(ASICameraInfo.CameraID, ASI_HARDWARE_BIN, lVal, ASI_FALSE);
	}
	else if (eAct == MM::BeforeGet)//����ֵ->�ؼ���ʾ
	{
		ASIGetControlValue(ASICameraInfo.CameraID, ASI_HARDWARE_BIN, &lVal, &bAuto);
		pProp->Set(lVal ? g_Keyword_on : g_Keyword_off);
	}
	return DEVICE_OK;
}





///////////////////////////////////////////////////////////////////////////////
// Private ASICamera methods
///////////////////////////////////////////////////////////////////////////////

void ASICamera::MallocControlCaps(int iCamindex)
{
	if (pControlCaps == NULL)
	{
		pControlCaps = new ASI_CONTROL_CAPS[iCtrlNum];
		for (int i = 0; i < iCtrlNum; i++)
		{
			ASIGetControlCaps(iCamindex, i, &pControlCaps[i]);
		}
	}
}
void ASICamera::DeletepControlCaps(int iCamindex)
{
	if (iCamindex < 0)
		return;
	if (pControlCaps)
	{
		delete[] pControlCaps;
		pControlCaps = NULL;
	}
}

bool ASICamera::isImgTypeSupported(ASI_IMG_TYPE ImgType)
{
	int i = 0;
	while (ASICameraInfo.SupportedVideoFormat[i] != ASI_IMG_END)
	{
		if (ASICameraInfo.SupportedVideoFormat[i] == ImgType)
			return true;
		i++;
	}
	return false;
}


ASI_CONTROL_CAPS* ASICamera::GetOneCtrlCap(int CtrlID)
{
	if (pControlCaps == 0)
		return 0;
	for (int i = 0; i < iCtrlNum; i++)
	{
		if (pControlCaps[i].ControlType == CtrlID)
			return &pControlCaps[i];
	}
	return 0;
}
void ASICamera::RefreshImgType()
{
	if (ImgType == ASI_IMG_RAW16)
	{
		iPixBytes = 2;
		iComponents = 1;
	}
	else if (ImgType == ASI_IMG_RGB24)
	{
		if (bRGB48)
		{
			iPixBytes = 8;
			iComponents = 4;
		}
		else
		{
			iPixBytes = 4;
			iComponents = 4;
		}

	}
	else
	{
		iPixBytes = 1;
		iComponents = 1;
	}
}


///////////////////////////////////////////////////////////////////////////////
// EFW implementation
// ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

CMyEFW::CMyEFW() :
	initialized_(false),
	bPosWait(false)
{
	InitializeDefaultErrorMessages();

	EFWInfo.ID = -1;

	// Description property
	int ret = CreateProperty(MM::g_Keyword_Description, "ZWO EFW filter wheel Device Adapter", MM::String, true);

	assert(ret == DEVICE_OK);

	iConnectedEFWNum = EFWGetNum();


	vector<string> EFWIndexValues;
	for (int i = 0; i < iConnectedEFWNum; i++)
	{
		EFWGetID(i, &EFWInfo.ID);
		sprintf(ConnectedEFWName[i], "EFW (ID %d)", EFWInfo.ID);//��������		
		EFWIndexValues.push_back(ConnectedEFWName[i]);
	}

	CPropertyAction* pAct = new CPropertyAction(this, &CMyEFW::OnSelectEFWIndex);//ͨ������ѡ��򿪵����
	if (iConnectedEFWNum > 0)
	{
		strcpy(sz_ModelIndex, ConnectedEFWName[0]);//Ĭ�ϴ򿪵�һ��
		//iCamIndex = 0;
		EFWGetID(0, &EFWInfo.ID);
	}
	else
	{
		strcpy(sz_ModelIndex, "no EFW connected");
	}
	//	strcpy(sz_ModelIndex, "DropDown");
	ret = CreateProperty(g_DeviceIndex, sz_ModelIndex, MM::String, false, pAct, true); //ѡ������ͷ���
	SetAllowedValues(g_DeviceIndex, EFWIndexValues);
	assert(ret == DEVICE_OK);
}

CMyEFW::~CMyEFW()
{
	Shutdown();
}

void CMyEFW::GetName(char* Name) const
{
	CDeviceUtils::CopyLimitedString(Name, g_StateDeviceName);
}


int CMyEFW::Initialize()
{
	OutputDbgPrint("CMyEFW::Initialize\n");
	if (initialized_)
		return DEVICE_OK;
	if (EFWInfo.ID < 0)
		return DEVICE_NOT_CONNECTED;

	if (EFWOpen(EFWInfo.ID) != ASI_SUCCESS)
		return DEVICE_NOT_CONNECTED;

	EFWGetProperty(EFWInfo.ID, &EFWInfo);
	// set property list
	// -----------------

	// Name
	int ret = CreateStringProperty(MM::g_Keyword_Name, g_StateDeviceName, true);
	if (DEVICE_OK != ret)
		return ret;

	// Description
/*	ret = CreateStringProperty(MM::g_Keyword_Description, "EFW driver", true);
	if (DEVICE_OK != ret)
		return ret;*/

		// create default positions and labels
	const int bufSize = 64;
	char buf[bufSize];
	for (int i = 0; i < EFWInfo.slotNum; i++)
	{
		snprintf(buf, bufSize, "position-%d", i + 1);
		SetPositionLabel(i, buf);
		//		AddAllowedValue(MM::g_Keyword_Closed_Position, buf);
	}

	// State
	// -----
	CPropertyAction* pAct = new CPropertyAction(this, &CMyEFW::OnState);
	ret = CreateProperty(MM::g_Keyword_State, "0", MM::Integer, false, pAct);
	assert(ret == DEVICE_OK);
	SetPropertyLimits(MM::g_Keyword_State, 0, EFWInfo.slotNum - 1);
	if (ret != DEVICE_OK)
		return ret;

	// Label
	// -----
	pAct = new CPropertyAction(this, &CStateBase::OnLabel);
	ret = CreateStringProperty(MM::g_Keyword_Label, "", false, pAct);
	if (ret != DEVICE_OK)
		return ret;

	ret = UpdateStatus();
	if (ret != DEVICE_OK)
		return ret;

	initialized_ = true;

	return DEVICE_OK;
}

bool CMyEFW::Busy()//����trueʱ��ˢ��label��state
{
	if (bPosWait)//
	{
		MM::MMTime interval = GetCurrentMMTime() - changedTime_;
		//	MM::MMTime delay(GetDelayMs()*1000.0);
		if (interval < 500 * 1000.0)
			return true;
	}
	int pos;
	EFW_ERROR_CODE err = EFWGetPosition(EFWInfo.ID, &pos);
	if (err != EFW_SUCCESS)
		return false;
	if (pos == -1)
	{
		//Sleep(500);
		changedTime_ = GetCurrentMMTime();
		bPosWait = true;
		return true;
	}
	else
	{
		bPosWait = false;
		return false;
	}
}


int CMyEFW::Shutdown()
{
	if (initialized_)
	{
		initialized_ = false;
	}
	EFWClose(EFWInfo.ID);
	return DEVICE_OK;
}

///////////////////////////////////////////////////////////////////////////////
// Action handlers
///////////////////////////////////////////////////////////////////////////////

int CMyEFW::OnState(MM::PropertyBase* pProp, MM::ActionType eAct)//CStateDeviceBase::OnLabel ���������
{
	if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		int pos;
		EFWGetPosition(EFWInfo.ID, &pos);
		if (pos == -1)
			pProp->Set(lLastPos);
		else
		{
			lLastPos = pos;
			pProp->Set(lLastPos);
		}
		// nothing to do, let the caller to use cached property
	}
	else if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ->����
	{
		// Set timer for the Busy signal
//		changedTime_ = GetCurrentMMTime();

		long pos;
		pProp->Get(pos);
		if (pos >= EFWInfo.slotNum || pos < 0)
		{
			//	pProp->Set(position_); // revert
			return DEVICE_INVALID_PROPERTY_VALUE;
		}
		else
			EFWSetPosition(EFWInfo.ID, pos);
	}

	return DEVICE_OK;
}


/**
* Handles "EFWIndex" property.
*/
int CMyEFW::OnSelectEFWIndex(MM::PropertyBase* pProp, MM::ActionType eAct)
{
	string str;
	if (eAct == MM::AfterSet)//�ӿؼ��õ�ѡ����ֵ
	{
		pProp->Get(str);
		for (int i = 0; i < iConnectedEFWNum; i++)
		{
			if (!str.compare(ConnectedEFWName[i]))
			{
				EFWGetID(i, &EFWInfo.ID);

				strcpy(sz_ModelIndex, ConnectedEFWName[i]);
				break;
			}
		}
	}
	else if (eAct == MM::BeforeGet)//ֵ���ؼ���ʾ
	{
		pProp->Set(sz_ModelIndex);
	}

	return DEVICE_OK;
}
unsigned long CMyEFW::GetNumberOfPositions() const
{
	return EFWInfo.slotNum;
}



