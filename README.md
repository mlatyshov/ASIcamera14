Device adapter for Micromanager 1.4, ready to work with the current ZWOASI SDK (v.1.36). \

# ASIcamera14  
**A modern ZWO ASI Camera & EFW Filter-Wheel device adapter for Micro-Manager 1.4**

[![GitHub release](https://img.shields.io/github/v/release/mlatyshov/ASIcamera14?label=latest%20release)](https://github.com/mlatyshov/ASIcamera14/releases)
[![CI](https://github.com/mlatyshov/ASIcamera14/actions/workflows/ci.yml/badge.svg)](https://github.com/mlatyshov/ASIcamera14/actions/workflows/ci.yml)

## Why this adapter exists

ZWO’s official Micro-Manager plug-in for the 1.4 branch has not been updated since **2016** and only works with the historical build *1.4.23_20160628* – newer nightly builds and recent ASI cameras (e.g. **ASI533/2600/585**) are not recognised —and ZWO confirmed they currently have **no plans to refresh the 1.4 driver**.:contentReference[oaicite:0]{index=0}  
If you maintain a legacy 1.4 workflow but need support for the latest ASI hardware, **ASIcamera14** fills the gap.

---

## Key features

* **Full camera line-up** — USB 2/3, cooled/uncool, mono/colour, including 2025 models driven by **ZWO ASI SDK v1.36**.  
* **EFW integration** — any 5/7-slot wheel supported out-of-the-box.  
* **High-speed streaming** — up to 500 fps with ring buffer & dedicated grab thread.  
* **Extended exposure control** — long exposures > 60 s, ROI, binning, gain/offset, USB-bandwidth throttling.  
* **Multi-D ready** — seamless operation inside Micro-Manager’s MDA (time-lapse, Z-stacks, multi-channel).  
* **Multi-camera** — index cameras by serial number and acquire in parallel.  
* **Cross-platform** — pre-built Windows 10/11 ×64 binaries; Makefile for Linux (Ubuntu 22.04 LTS).  
* **Open source (MIT)** — actively maintained, welcoming PRs and issue reports.

---

## Supported stack

| Component | Version | Notes |
|-----------|---------|-------|
| **Micro-Manager** | 1.4.23 & 1.4.24 nightly builds | Other 1.4.x builds may work but are not tested. |
| **ZWO ASI SDK** | 1.36 (bundled) | Newer SDKs will be evaluated as they appear. |
| **ZWO EFW SDK** | 1.6+ | Optional – only needed for filter wheels. |

---

## Quick start (Windows x64)

1. Download **`ASIcamera14-win64.zip`** from the *Releases* page and copy  
   `mmgr_dal_ASICamera.dll` into your Micro-Manager folder.  
2. From the official SDKs copy **`ASICamera2.dll`** and **`EFW_filter.dll`** to the same folder.:contentReference[oaicite:1]{index=1}  
3. Launch Micro-Manager → **Devices ► Hardware Configuration Wizard**.  
4. Add **`ASI Camera`** (and **`ASI FilterWheel`** if present), pick the desired device index, finish the wizard.  
5. Hit **Live** – you’re ready to capture.

### Linux build

```bash
git clone https://github.com/mlatyshov/ASIcamera14.git
cd ASIcamera14
make          # requires libasicamera2.so & libEFWFilter.so in your search path
sudo make install

### For questions

Releases available for versions 1.14.23 and 1.14.24.\
For questions, contact tg@mlatyshov
