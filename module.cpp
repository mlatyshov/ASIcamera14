///////////////////////////////////////////////////////////////////////////////
// FILE:          module.cpp
// PROJECT:       Micro-Manager
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

#include "../../MMDevice/ModuleInterface.h"

#include "ASICamera.h"

// Exported MMDevice API
MODULE_API void InitializeModuleData()
{
	RegisterDevice(cameraName, MM::CameraDevice, "ASI camera ZWO ml");
	RegisterDevice(g_StateDeviceName, MM::StateDevice, "ZWO EFW filter wheel");
}

MODULE_API MM::Device* CreateDevice(const char* deviceName)
{
	if (deviceName == 0)
		return 0;

	std::string deviceName_(deviceName);

	if (deviceName_ == cameraName)
	{
		ASICamera* s = new ASICamera();
		return s;
	}
	else if (deviceName_ == g_StateDeviceName)
	{
		CMyEFW* efw = new CMyEFW();
		return efw;
	}
	
	return 0;
}

MODULE_API void DeleteDevice(MM::Device* pDevice)
{
	delete pDevice;
}