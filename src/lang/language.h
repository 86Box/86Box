/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Definitions for the language management module.
 *
 * NOTE:	FIXME: Strings 2176 and 2193 are same.
 *
 * Version:	@(#)language.h	1.0.6	2018/01/23
 *
 * Author:	Fred N. van Kempen, <decwiz@yahoo.com>
 *
 *		Copyright 2017,2018 Fred N. van Kempen.
 */
#ifndef LANG_UAGE_H
# define LANG_UAGE_H


/* String IDs. */
#define IDS_STRINGS	2048		// "86Box"
#define IDS_2049	2049		// "86Box Error"
#define IDS_2050	2050		// "86Box Fatal Error"
#define IDS_2051	2051		// "This will reset 86Box.."
#define IDS_2052	2052		// "DirectDraw Screenshot Error"
#define IDS_2053	2053		// "Invalid number of sectors.."
#define IDS_2054	2054		// "Invalid number of heads.."
#define IDS_2055	2055		// "Invalid number of cylinders.."
#define IDS_2056	2056		// "No usable ROM images found!"
#define IDS_2057	2057		// "(empty)"
#define IDS_2058	2058		// "(host drive %c:)"
#define IDS_2059	2059		// "(Turbo)"
#define IDS_2060	2060		// "On"
#define IDS_2061	2061		// "Off"
#define IDS_2062	2062		// "86Box was unable to find any.."
#define IDS_2063	2063		// "Configured ROM set not avai.."
#define IDS_2064	2064		// "Configured video BIOS not.."
#define IDS_2065	2065		// "Machine"
#define IDS_2066	2066		// "Display"
#define IDS_2067	2067		// "Input devices"
#define IDS_2068	2068		// "Sound"
#define IDS_2069	2069		// "Network"
#define IDS_2070	2070		// "Ports (COM & LPT)"
#define IDS_2071	2071		// "Other peripherals"
#define IDS_2072	2072		// "Hard disks"
#define IDS_2073	2073		// "Floppy drives"
#define IDS_2074	2074		// "Other removable devices"
#define IDS_2075	2075		// "CD-ROM images (*.ISO;*.CU.."
#define IDS_2076	2076		// "Host CD/DVD Drive (%c:)"
#define IDS_2077	2077		// "Click to capture mouse"
#define IDS_2078	2078		// "Press F12-F8 to release mouse"
#define IDS_2079	2079		// "Press F12-F8 or middle button.."
#define IDS_2080	2080		// "Drive"
#define IDS_2081	2081		// "Location"
#define IDS_2082	2082		// "Bus"
#define IDS_2083	2083		// "File"
#define IDS_2084	2084		// "C"
#define IDS_2085	2085		// "H"
#define IDS_2086	2086		// "S"
#define IDS_2087	2087		// "MB"
#define IDS_2088	2088		// "Unable to create bitmap file: %s"
#define IDS_2089	2089		// "Enabled"
#define IDS_2090	2090		// "Mute"
#define IDS_2091	2091		// "Type"
#define IDS_2092	2092		// "Bus"
#define IDS_2093	2093		// "DMA"
#define IDS_2094	2094		// "KB"
#define IDS_2095	2095		// "Neither DirectDraw nor Dire.."
#define IDS_2096	2096		// "Slave"
#define IDS_2097	2097		// "SCSI (ID %s, LUN %s)"
#define IDS_2098	2098		// "Adapter Type"
#define IDS_2099	2099		// "Base Address"
#define IDS_2100	2100		// "IRQ"
#define IDS_2101	2101		// "8-bit DMA"
#define IDS_2102	2102		// "16-bit DMA"
#define IDS_2103	2103		// "BIOS"
#define IDS_2104	2104		// "Network Type"
#define IDS_2105	2105		// "Surround Module"
#define IDS_2106	2106		// "MPU-401 Base Address"
#define IDS_2107	2107		// "Use CTRL+ALT+PAGE DOWN.."
#define IDS_2108	2108		// "On-board RAM"
#define IDS_2109	2109		// "Memory Size"
#define IDS_2110	2110		// "Display Type"
#define IDS_2111	2111		// "RGB"
#define IDS_2112	2112		// "Composite"
#define IDS_2113	2113		// "Composite Type"
#define IDS_2114	2114		// "Old"
#define IDS_2115	2115		// "New"
#define IDS_2116	2116		// "RGB Type"
#define IDS_2117	2117		// "Color"
#define IDS_2118	2118		// "Monochrome (Green)"
#define IDS_2119	2119		// "Monochrome (Amber)"
#define IDS_2120	2120		// "Monochrome (Gray)"
#define IDS_2121	2121		// "Color (no brown)"
#define IDS_2122	2122		// "Monochrome (Default)"
#define IDS_2123	2123		// "Snow Emulation"
#define IDS_2124	2124		// "Bilinear Filtering"
#define IDS_2125	2125		// "Dithering"
#define IDS_2126	2126		// "Framebuffer Memory Size"
#define IDS_2127	2127		// "Texture Memory Size"
#define IDS_2128	2128		// "Screen Filter"
#define IDS_2129	2129		// "Render Threads"
#define IDS_2130	2130		// "Recompiler"
#define IDS_2131	2131		// "System Default"
#define IDS_2132	2132		// "%i Wait state(s)"
#define IDS_2133	2133		// "8-bit"
#define IDS_2134	2134		// "Slow 16-bit"
#define IDS_2135	2135		// "Fast 16-bit"
#define IDS_2136	2136		// "Slow VLB/PCI"
#define IDS_2137	2137		// "Mid  VLB/PCI"
#define IDS_2138	2138		// "Fast VLB/PCI"
#define IDS_2139	2139		// "PCap failed to set up.."
#define IDS_2140	2140		// "No PCap devices found"
#define IDS_2141	2141		// "Invalid PCap device"
#define IDS_2142	2142		// "&Notify disk change"
#define IDS_2143	2143		// "Type"
#define IDS_2144	2144		// "Standard 2-button joystick(s)"
#define IDS_2145	2145		// "Standard 4-button joystick"
#define IDS_2146	2146		// "Standard 6-button joystick"
#define IDS_2147	2147		// "Standard 8-button joystick"
#define IDS_2148	2148		// "CH Flightstick Pro"
#define IDS_2149	2149		// "Microsoft SideWinder Pad"
#define IDS_2150	2150		// "Thrustmaster Flight Cont.."
#define IDS_2151	2151		// "Disabled"
#define IDS_2152	2152		// "None"
#define IDS_2153	2153		// "Unable to load Accelerators"
#define IDS_2154	2154		// "Unable to register Raw Input"
#define IDS_2155	2155		// "IRQ %i"
#define IDS_2156	2156		// "%" PRIu64
#define IDS_2157	2157		// "%" PRIu64 " MB (CHS: %".."
#define IDS_2158	2158		// "Floppy %i (%s): %ls"
#define IDS_2159	2159		// "All floppy images (*.0??;*.."
#define IDS_2160	2160		// "Configuration files (*.CF.."
#define IDS_2161	2161		// "&New image..."
#define IDS_2162	2162		// "&Existing image..."
#define IDS_2163	2163		// "Existing image (&Write-pr..."
#define IDS_2164	2164		// "E&ject"
#define IDS_2165	2165		// "&Mute"
#define IDS_2166	2166		// "E&mpty"
#define IDS_2167	2167		// "&Reload previous image"
#define IDS_2168	2168		// "&Image..."
#define IDS_2169	2169		// "Image (&Write-protected)..."
#define IDS_2170	2170		// "Check BPB"
#define IDS_2171	2171		// "Unable to initialize Flui.."
#define IDS_2172	2172		// "E&xport to 86F..."
#define IDS_2173	2173		// "Surface-based images (*.8.."
#define IDS_2174	2174		// "All floppy images (*.DSK..."
#define IDS_2175	2175		// "ZIP images (*.IM?)\0*.IM..."
#define IDS_2176	2176		// "ZIP images (*.IM?)\0*.IM..."
#define IDS_2177	2177		// "ZIP %i (%03i): %ls"

#define IDS_4096	4096		// "Hard disk (%s)"
#define IDS_4097	4097		// "%01i:%01i"
#define IDS_4098	4098		// "%i"
#define IDS_4099	4099		// "Disabled"
#define IDS_4100	4100		// "Custom..."
#define IDS_4101	4101		// "Custom (large)..."
#define IDS_4102	4102		// "Add New Hard Disk"
#define IDS_4103	4103		// "Add Existing Hard Disk"
#define IDS_4104	4104		// "Attempting to create a HDI ima.."
#define IDS_4105	4105		// "Attempting to create a spurio.."
#define IDS_4106	4106		// "Hard disk images (*.HDI;*.HD.."
#define IDS_4107	4107		// "Unable to open the file for read"
#define IDS_4108	4108		// "Unable to open the file for write"
#define IDS_4109	4109		// "HDI or HDX image with a sect.."
#define IDS_4110	4110		// "USB is not yet supported"
#define IDS_4111	4111		// "This image exists and will be.."
#define IDS_4112	4112		// "Please enter a valid file name"
#define IDS_4113	4113		// "Remember to partition and fo.."
#define IDS_4114	4114		// "MFM/RLL or ESDI CD-ROM driv.."
#define IDS_4115	4115		// "Removable disk %i (SCSI): %ls"

#define IDS_4352	4352		// "MFM/RLL"
#define IDS_4353	4353		// "XT IDE"
#define IDS_4354	4354		// "ESDI"
#define IDS_4355	4355		// "IDE (PIO-only)"
#define IDS_4356	4356		// "IDE (PIO+DMA)"
#define IDS_4357	4357		// "SCSI"
#define IDS_4358	4358		// "SCSI (removable)"

#define IDS_4608	4608		// "MFM/RLL (%01i:%01i)"
#define IDS_4609	4609		// "XT IDE (%01i:%01i)"
#define IDS_4610	4610		// "ESDI (%01i:%01i)"
#define IDS_4611	4611		// "IDE (PIO-only) (%01i:%01i)"
#define IDS_4612	4612		// "IDE (PIO+DMA) (%01i:%01i)"
#define IDS_4613	4613		// "SCSI (%02i:%02i)"
#define IDS_4614	4614		// "SCSI (removable) (%02i:%02i)"

#define IDS_5120	5120		// "CD-ROM %i (%s): %s"

#define IDS_5376	5376		// "Disabled"
#define IDS_5377	5377		// "<Reserved>"
#define IDS_5378	5378		// "<Reserved>"
#define IDS_5379	5379		// "<Reserved>"
#define IDS_5380	5380		// "ATAPI (PIO-only)"
#define IDS_5381	5381		// "ATAPI (PIO and DMA)"
#define IDS_5382	5382		// "SCSI"

#define IDS_5632	5632		// "Disabled"
#define IDS_5633	5633		// "<Reserved>"
#define IDS_5634	5634		// "<Reserved>"
#define IDS_5635	5635		// "<Reserved>"
#define IDS_5636	5636		// "ATAPI (PIO-only) (%01i:%01i)"
#define IDS_5637	5637		// "ATAPI (PIO and DMA) (%01i:%01i)"
#define IDS_5638	5638		// "SCSI (%02i:%02i)"

#define IDS_5888	5888		// "160 kB"
#define IDS_5889	5889		// "180 kB"
#define IDS_5890	5890		// "320 kB"
#define IDS_5891	5891		// "360 kB"
#define IDS_5892	5892		// "640 kB"
#define IDS_5893	5893		// "720 kB"
#define IDS_5894	5894		// "1.2 MB"
#define IDS_5895	5895		// "1.25 MB"
#define IDS_5896	5896		// "1.44 MB"
#define IDS_5897	5897		// "DMF (cluster 1024)"
#define IDS_5898	5898		// "DMF (cluster 2048)"
#define IDS_5899	5899		// "2.88 MB"
#define IDS_5900	5900		// "ZIP 100"
#define IDS_5901	5901		// "ZIP 250"

#define IDS_6144	6144		// "Perfect RPM"
#define IDS_6145	6145		// "1%% below perfect RPM"
#define IDS_6146	6146		// "1.5%% below perfect RPM"
#define IDS_6147	6147		// "2%% below perfect RPM"

#define IDS_7168	7168		// "English (United States)"

#define IDS_LANG_ENUS	IDS_7168

#define STR_NUM_2048	130
#define STR_NUM_3072	11
#define STR_NUM_4096	20
#define STR_NUM_4352	7
#define STR_NUM_4608	7
#define STR_NUM_5120	1
#define STR_NUM_5376	7
#define STR_NUM_5632	7
#define STR_NUM_5888	14
#define STR_NUM_6144	4
#define STR_NUM_7168	1


#endif	/*LANG_UAGE_H*/
