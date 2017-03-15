/* Copyright holders: Sarah Walker, Tenshi
   see COPYING for more details
*/
#include <inttypes.h>
#define BITMAP WINDOWS_BITMAP
#include <windows.h>
#include <windowsx.h>
#undef BITMAP

#include "ibm.h"
#include "ide.h"
#include "resources.h"
#include "win.h"

static int hd_changed = 0;

static char hd_new_name[512];
static uint64_t hd_new_spt, hd_new_hpc, hd_new_cyl;
static int hd_new_hdi;
static int new_cdrom_channel;

int hdnew_no_update = 0;

hard_disk_t hdnew_temp_hd;

int hdsize_no_update = 0;

hard_disk_t hdsize_temp_hd;

char s[260];

static int hdconf_initialize_hdt_combo(HWND hdlg, hard_disk_t *internal_hd)
{
        HWND h;
	int i = 0;
	uint64_t size = 0;
	uint64_t size_mb = 0;
	uint64_t size_shift = 11;
	int selection = 127;

	h = GetDlgItem(hdlg, IDC_COMBOHDT);
	for (i = 0; i < 127; i++)
	{	
		size = hdt[i][0] * hdt[i][1] * hdt[i][2];
		size_mb = size >> size_shift;
                sprintf(s, "%" PRIu64 " MB (CHS: %" PRIu64 ", %" PRIu64 ", %" PRIu64 ")", size_mb, hdt[i][0], hdt[i][1], hdt[i][2]);
		SendMessage(h, CB_ADDSTRING, 0, (LPARAM)s);
		if ((internal_hd->tracks == hdt[i][0]) && (internal_hd->hpc == hdt[i][1]) && (internal_hd->spt == hdt[i][2]))
		{
			selection = i;
		}
	}
	sprintf(s, "Custom...");
	SendMessage(h, CB_ADDSTRING, 0, (LPARAM)s);
	SendMessage(h, CB_SETCURSEL, selection, 0);
	return selection;
}

static void hdconf_update_text_boxes(HWND hdlg, BOOL enable)
{
	HWND h;

	h=GetDlgItem(hdlg, IDC_EDIT1);
	EnableWindow(h, enable);
	h=GetDlgItem(hdlg, IDC_EDIT2);
	EnableWindow(h, enable);
	h=GetDlgItem(hdlg, IDC_EDIT3);
	EnableWindow(h, enable);
	h=GetDlgItem(hdlg, IDC_EDIT4);
	EnableWindow(h, enable);
}

void hdconf_set_text_boxes(HWND hdlg, hard_disk_t *internal_hd)
{
	HWND h;

	uint64_t size_shift = 11;

	h = GetDlgItem(hdlg, IDC_EDIT1);
	sprintf(s, "%" PRIu64, internal_hd->spt);
	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
	h = GetDlgItem(hdlg, IDC_EDIT2);
	sprintf(s, "%" PRIu64, internal_hd->hpc);
	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
	h = GetDlgItem(hdlg, IDC_EDIT3);
	sprintf(s, "%" PRIu64, internal_hd->tracks);
	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

	h = GetDlgItem(hdlg, IDC_EDIT4);
	sprintf(s, "%" PRIu64, (internal_hd->spt * internal_hd->hpc * internal_hd->tracks) >> size_shift);
	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
}

BOOL hdconf_initdialog_common(HWND hdlg, hard_disk_t *internal_hd, int *no_update, uint64_t spt, uint64_t hpc, uint64_t tracks)
{
	HWND h;
	int selection = 127;

	internal_hd->spt = spt;
	internal_hd->hpc = hpc;
	internal_hd->tracks = tracks;
	*no_update = 1;

	hdconf_set_text_boxes(hdlg, internal_hd);

	selection = hdconf_initialize_hdt_combo(hdlg, internal_hd);

	if (selection < 127)
	{
		hdconf_update_text_boxes(hdlg, FALSE);
	}
	else
	{
		hdconf_update_text_boxes(hdlg, TRUE);
	}

	*no_update = 0;

	return TRUE;
}

int hdconf_idok_common(HWND hdlg)
{
	HWND h;

	h = GetDlgItem(hdlg, IDC_EDIT1);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &hd_new_spt);
	h = GetDlgItem(hdlg, IDC_EDIT2);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &hd_new_hpc);
	h = GetDlgItem(hdlg, IDC_EDIT3);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &hd_new_cyl);
                        
	if (hd_new_spt > 63)
	{
		MessageBox(ghwnd, "Drive has too many sectors (maximum is 63)", "86Box error", MB_OK);
		return 1;
	}
	if (hd_new_hpc > 16)
	{
		MessageBox(ghwnd, "Drive has too many heads (maximum is 16)", "86Box error", MB_OK);
		return 1;
	}
	if (hd_new_cyl > 266305)
	{
		MessageBox(ghwnd, "Drive has too many cylinders (maximum is 266305)", "86Box error", MB_OK);
		return 1;
	}

	return 0;
}

BOOL hdconf_process_edit_boxes(HWND hdlg, WORD control, uint64_t *var, hard_disk_t *internal_hd, int *no_update)
{
	HWND h;
	uint64_t size_shift = 11;

	if (*no_update)
	{
		return FALSE;
	}

	h = GetDlgItem(hdlg, control);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, var);

	*no_update = 1;
	if(!(*var))
	{
		*var = 1;
		sprintf(s, "%" PRIu64, *var);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
	}

	if (control == IDC_EDIT4)
	{
		*var <<= 11;			/* Convert to sectors */
		*var /= internal_hd->hpc;
		*var /= internal_hd->spt;
		internal_hd->tracks = *var;

		h = GetDlgItem(hdlg, IDC_EDIT3);
		sprintf(s, "%" PRIu64, internal_hd->tracks);
		SendMessage(h, WM_SETTEXT, 1, (LPARAM)s);
	}
	else if ((control >= IDC_EDIT1) && (control <= IDC_EDIT3))
	{
		h = GetDlgItem(hdlg, IDC_EDIT4);
		sprintf(s, "%" PRIu64, (internal_hd->spt * internal_hd->hpc * internal_hd->tracks) >> size_shift);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
	}

	*no_update = 0;
	return TRUE;
}

BOOL hdconf_process_hdt_combo(HWND hdlg, hard_disk_t *internal_hd, int *no_update, WPARAM wParam)
{
	HWND h;
	int selection = 127;

	if (*no_update)
	{
		return FALSE;
	}

	if (HIWORD(wParam) == CBN_SELCHANGE)
	{
		*no_update = 1;

		h = GetDlgItem(hdlg,IDC_COMBOHDT);
		selection = SendMessage(h,CB_GETCURSEL,0,0);

		if (selection < 127)
		{
			hdconf_update_text_boxes(hdlg, FALSE);

			internal_hd->tracks = hdt[selection][0];
			internal_hd->hpc = hdt[selection][1];
			internal_hd->spt = hdt[selection][2];

			hdconf_set_text_boxes(hdlg, internal_hd);
		}
		else
		{
			hdconf_update_text_boxes(hdlg, TRUE);
		}

		*no_update = 0;
	}
	else
	{
		return FALSE;
	}

	return TRUE;
}

BOOL CALLBACK hdconf_common_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam, int type, hard_disk_t *internal_hd, int *no_update, uint64_t spt, uint64_t hpc, uint64_t tracks)
{
        HWND h;
        uint64_t c;
        FILE *f;
        uint8_t buf[512];
	int is_hdi;
	int is_hdx;
	uint64_t size;
	uint64_t signature = 0xD778A82044445459;
	uint32_t zero = 0;
	uint32_t sector_size = 512;
	uint32_t base = 0x1000;
	uint64_t full_size = 0;
	uint64_t full_size_bytes = 0;
	uint64_t size_shift = 11;
	int selection = 127;
        switch (message)
        {
                case WM_INITDIALOG:
		if (!type)
		{
	                h = GetDlgItem(hdlg, IDC_EDITC);
        	        SendMessage(h, WM_SETTEXT, 0, (LPARAM)"");
		}
		return hdconf_initdialog_common(hdlg, internal_hd, no_update, spt, hpc, tracks);

                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
			if (!type)
			{
	                        h = GetDlgItem(hdlg, IDC_EDITC);
        	                SendMessage(h, WM_GETTEXT, 511, (LPARAM)hd_new_name);
                	        if (!hd_new_name[0])
                        	{
                                	MessageBox(ghwnd,"Please enter a valid filename","86Box error",MB_OK);
	                                return TRUE;
        	                }
			}

			if (hdconf_idok_common(hdlg))
			{
				return TRUE;
			}

			if (!type)
			{
                	        f = fopen64(hd_new_name, "wb");
        	                if (!f)
	                        {
                                	MessageBox(ghwnd, "Can't open file for write", "86Box error", MB_OK);
                        	        return TRUE;
                	        }
				full_size = (hd_new_cyl * hd_new_hpc * hd_new_spt);
				full_size_bytes = full_size * 512;
				if (image_is_hdi(hd_new_name))
				{
					if (full_size_bytes >= 0x100000000)
					{
						MessageBox(ghwnd, "Drive is HDI and 4 GB or bigger (size filed in HDI header is 32-bit)", "86Box error", MB_OK);
						fclose(f);
						return TRUE;
					}

					hd_new_hdi = 1;

					fwrite(&zero, 1, 4, f);			/* 00000000: Zero/unknown */
					fwrite(&zero, 1, 4, f);			/* 00000004: Zero/unknown */
					fwrite(&base, 1, 4, f);			/* 00000008: Offset at which data starts */
					fwrite(&full_size_bytes, 1, 4, f);	/* 0000000C: Full size of the data (32-bit) */
					fwrite(&sector_size, 1, 4, f);		/* 00000010: Sector size in bytes */
					fwrite(&hd_new_spt, 1, 4, f);		/* 00000014: Sectors per cylinder */
					fwrite(&hd_new_hpc, 1, 4, f);		/* 00000018: Heads per cylinder */
					fwrite(&hd_new_cyl, 1, 4, f);		/* 0000001C: Cylinders */

					for (c = 0; c < 0x3f8; c++)
					{
						fwrite(&zero, 1, 4, f);
					}
				}
				else if (image_is_hdx(hd_new_name, 0))
				{
					if (full_size_bytes > 0xffffffffffffffff)
					{
						MessageBox(ghwnd, "Drive is HDX and way too big (size filed in HDX header is 64-bit)", "86Box error", MB_OK);
						fclose(f);
						return TRUE;
					}

					hd_new_hdi = 1;

					fwrite(&signature, 1, 8, f);		/* 00000000: Signature */
					fwrite(&full_size_bytes, 1, 8, f);	/* 00000008: Full size of the data (64-bit) */
					fwrite(&sector_size, 1, 4, f);		/* 00000010: Sector size in bytes */
					fwrite(&hd_new_spt, 1, 4, f);		/* 00000014: Sectors per cylinder */
					fwrite(&hd_new_hpc, 1, 4, f);		/* 00000018: Heads per cylinder */
					fwrite(&hd_new_cyl, 1, 4, f);		/* 0000001C: Cylinders */
					fwrite(&zero, 1, 4, f);			/* 00000020: [Translation] Sectors per cylinder */
					fwrite(&zero, 1, 4, f);			/* 00000004: [Translation] Heads per cylinder */
				}
	                        memset(buf, 0, 512);
                	        for (c = 0; c < full_size; c++)
        	                    fwrite(buf, 512, 1, f);
	                        fclose(f);
                        
	                        MessageBox(ghwnd, "Remember to partition and format the new drive", "86Box", MB_OK);
			}
                        
                        EndDialog(hdlg,1);
                        return TRUE;

                        case IDCANCEL:
                        EndDialog(hdlg, 0);
                        return TRUE;

                        case IDC_CFILE:
			if (!type)
			{
	                        if (!getsfile(hdlg, "Hard disc image (*.HDI;*.HDX;*.IMA;*.IMG)\0*.HDI;*.HDX;*.IMA;*.IMG\0All files (*.*)\0*.*\0", ""))
        	                {
                	                h = GetDlgItem(hdlg, IDC_EDITC);
                        	        SendMessage(h, WM_SETTEXT, 0, (LPARAM)openfilestring);
	                        }
	                        return TRUE;
			}
			else
			{
				break;
			}

                        case IDC_EDIT1:
			return hdconf_process_edit_boxes(hdlg, IDC_EDIT1, &(internal_hd->spt), internal_hd, no_update);

			case IDC_EDIT2:
			return hdconf_process_edit_boxes(hdlg, IDC_EDIT2, &(internal_hd->hpc), internal_hd, no_update);

			case IDC_EDIT3:
			return hdconf_process_edit_boxes(hdlg, IDC_EDIT3, &(internal_hd->tracks), internal_hd, no_update);

			case IDC_EDIT4:
			return hdconf_process_edit_boxes(hdlg, IDC_EDIT4, &size, internal_hd, no_update);

			case IDC_COMBOHDT:
			return hdconf_process_hdt_combo(hdlg, internal_hd, no_update, wParam);
                }
                break;

        }
        return FALSE;
}

static BOOL CALLBACK hdnew_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	return hdconf_common_dlgproc(hdlg, message, wParam, lParam, 0, &hdnew_temp_hd, &hdnew_no_update, 63, 16, 511);
}

BOOL CALLBACK hdsize_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	return hdconf_common_dlgproc(hdlg, message, wParam, lParam, 1, &hdsize_temp_hd, &hdsize_no_update, hd_new_spt, hd_new_hpc, hd_new_cyl);
}

static void hdconf_eject(HWND hdlg, int drive_num, hard_disk_t *hd)
{
	hd->spt = 0;
	hd->hpc = 0;
	hd->tracks = 0;
	ide_fn[drive_num][0] = 0;
	SetDlgItemText(hdlg, IDC_EDIT_C_SPT + drive_num, "0");
	SetDlgItemText(hdlg, IDC_EDIT_C_HPC + drive_num, "0");
	SetDlgItemText(hdlg, IDC_EDIT_C_CYL + drive_num, "0");
	SetDlgItemText(hdlg, IDC_EDIT_C_FN + drive_num, "");
	hd_changed = 1;
	return;
}

static void hdconf_new(HWND hdlg, int drive_num)
{
        HWND h;

	if (DialogBox(hinstance, TEXT("HdNewDlg"), hdlg, hdnew_dlgproc) == 1)
	{
		h = GetDlgItem(hdlg, IDC_EDIT_C_SPT + drive_num);
		sprintf(s, "%" PRIu64, hd_new_spt);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
		h = GetDlgItem(hdlg, IDC_EDIT_C_HPC + drive_num);
		sprintf(s, "%" PRIu64, hd_new_hpc);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
		h = GetDlgItem(hdlg, IDC_EDIT_C_CYL + drive_num);
		sprintf(s, "%" PRIu64, hd_new_cyl);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
		h = GetDlgItem(hdlg, IDC_EDIT_C_FN + drive_num);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)hd_new_name);

		h=  GetDlgItem(hdlg, IDC_TEXT_C_SIZE + drive_num);
		sprintf(s, "Size: %" PRIu64 " MB", (hd_new_cyl*hd_new_hpc*hd_new_spt) >> 11);
		SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);

		hd_changed = 1;
	}
	return;
}

static void hdconf_file(HWND hdlg, int drive_num)
{
        HWND h;
        FILE *f;
        off64_t sz;
	uint32_t sector_size = 512;
	uint32_t base = 0x1000;
	int ret;

	if (!getfile(hdlg, "Hard disc image (*.HDI;*.HDX;*.IMA;*.IMG;*.VHD)\0*.HDI;*.HDX;*.IMA;*.IMG;*.VHD\0All files (*.*)\0*.*\0", ""))
	{
		f = fopen64(openfilestring, "rb");
		if (!f)
		{
			MessageBox(ghwnd,"Can't open file for read","86Box error",MB_OK);
			return;
		}

		if (image_is_hdi(openfilestring) || image_is_hdx(openfilestring, 1))
		{
			fseeko64(f, 0x10, SEEK_SET);
			fread(&sector_size, 1, 4, f);
			if (sector_size != 512)
			{
				MessageBox(ghwnd,"HDI or HDX image with a sector size that is not 512","86Box error",MB_OK);
				fclose(f);
				return;
			}
			fread(&hd_new_spt, 1, 4, f);
			fread(&hd_new_hpc, 1, 4, f);
			fread(&hd_new_cyl, 1, 4, f);

			ret = 1;
		}
		else
		{
			fseeko64(f, -1, SEEK_END);
			sz = ftello64(f) + 1;
			fclose(f);
			hd_new_spt = 63;
			hd_new_hpc = 16;
			hd_new_cyl = ((sz / 512) / 16) / 63;

			ret = DialogBox(hinstance, TEXT("HdSizeDlg"), hdlg, hdsize_dlgproc);
		}
		if (ret == 1)
		{
			h = GetDlgItem(hdlg, IDC_EDIT_C_SPT + drive_num);
			sprintf(s, "%" PRIu64, hd_new_spt);
			SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
			h = GetDlgItem(hdlg, IDC_EDIT_C_HPC + drive_num);
			sprintf(s, "%" PRIu64, hd_new_hpc);
			SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
			h = GetDlgItem(hdlg, IDC_EDIT_C_CYL + drive_num);
			sprintf(s, "%" PRIu64, hd_new_cyl);
			SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
			h = GetDlgItem(hdlg, IDC_EDIT_C_FN + drive_num);
			SendMessage(h, WM_SETTEXT, 0, (LPARAM)openfilestring);

			h = GetDlgItem(hdlg, IDC_TEXT_C_SIZE + drive_num);
			sprintf(s, "Size: %" PRIu64 " MB", (hd_new_cyl*hd_new_hpc*hd_new_spt) >> 11);
			SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
        
			hd_changed = 1;
		}
	}
	return;
}

static void hdconf_edit_boxes(HWND hdlg, int drive_num, hard_disk_t *hd)
{
        HWND h;

	h = GetDlgItem(hdlg, IDC_EDIT_C_SPT + drive_num);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &(hd->spt));
	h = GetDlgItem(hdlg, IDC_EDIT_C_HPC + drive_num);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &(hd->hpc));
	h = GetDlgItem(hdlg, IDC_EDIT_C_CYL + drive_num);
	SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	sscanf(s, "%" PRIu64, &(hd->tracks));

	h = GetDlgItem(hdlg, IDC_TEXT_C_SIZE + drive_num);
	sprintf(s, "Size: %" PRIu64 " MB", (hd->tracks*hd->hpc*hd->spt) >> 11);
	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
	return;
}

static BOOL CALLBACK hdconf_dlgproc(HWND hdlg, UINT message, WPARAM wParam, LPARAM lParam)
{
        HWND h;
        hard_disk_t hd[IDE_NUM];
	int drive_num = 0;
        switch (message)
        {
                case WM_INITDIALOG:
                pause = 1;

		for (drive_num = 0; drive_num < IDE_NUM; drive_num++)
                {
	                hd[drive_num] = hdc[drive_num];

                	h = GetDlgItem(hdlg, IDC_EDIT_C_SPT + drive_num);
                	sprintf(s, "%" PRIu64, hdc[drive_num].spt);
                	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                	h = GetDlgItem(hdlg, IDC_EDIT_C_HPC + drive_num);
                	sprintf(s, "%" PRIu64, hdc[drive_num].hpc);
                	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                	h = GetDlgItem(hdlg, IDC_EDIT_C_CYL + drive_num);
                	sprintf(s, "%" PRIu64, hdc[drive_num].tracks);
                	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
                	h = GetDlgItem(hdlg, IDC_EDIT_C_FN + drive_num);
                	SendMessage(h, WM_SETTEXT, 0, (LPARAM)ide_fn[drive_num]);
                
                	h = GetDlgItem(hdlg, IDC_TEXT_C_SIZE + drive_num);
                	sprintf(s, "Size: %" PRIu64 " MB", (hd[drive_num].tracks*hd[drive_num].hpc*hd[drive_num].spt) >> 11);
                	SendMessage(h, WM_SETTEXT, 0, (LPARAM)s);
		}

                hd_changed = 0;

                return TRUE;
                
                case WM_COMMAND:
                switch (LOWORD(wParam))
                {
                        case IDOK:
                        if (hd_changed)
                        {                     
                                if (MessageBox(NULL, "This will reset 86Box!\nOkay to continue?", "86Box", MB_OKCANCEL) == IDOK)
                                {
					for (drive_num = 0; drive_num < IDE_NUM; drive_num++)
					{
	                                        h = GetDlgItem(hdlg, IDC_EDIT_C_SPT + drive_num);
        	                                SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
                	                        sscanf(s, "%" PRIu64, &hd[drive_num].spt);
	                                        h = GetDlgItem(hdlg, IDC_EDIT_C_HPC + drive_num);
	                                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	                                        sscanf(s, "%" PRIu64, &hd[drive_num].hpc);
	                                        h = GetDlgItem(hdlg, IDC_EDIT_C_CYL + drive_num);
	                                        SendMessage(h, WM_GETTEXT, 255, (LPARAM)s);
	                                        sscanf(s, "%" PRIu64, &hd[drive_num].tracks);
	                                        h = GetDlgItem(hdlg, IDC_EDIT_C_FN + drive_num);
	                                        SendMessage(h, WM_GETTEXT, 511, (LPARAM)ide_fn[drive_num]);
                                        
                                        	hdc[drive_num] = hd[drive_num];
					}

                                        saveconfig();
                                                                                
                                        resetpchard();
                                }                                
                        }
                        case IDCANCEL:
                        EndDialog(hdlg, 0);
                        pause = 0;
                        return TRUE;

                        case IDC_EJECTC:
                        case IDC_EJECTD:
                        case IDC_EJECTE:
                        case IDC_EJECTF:
                        case IDC_EJECTG:
                        case IDC_EJECTH:
                        case IDC_EJECTI:
                        case IDC_EJECTJ:
				drive_num = LOWORD(wParam) % 10;
				hdconf_eject(hdlg, drive_num, &(hd[drive_num]));
	                        return TRUE;
                        
                        case IDC_CNEW:
                        case IDC_DNEW:
                        case IDC_ENEW:
                        case IDC_FNEW:
                        case IDC_GNEW:
                        case IDC_HNEW:
                        case IDC_INEW:
                        case IDC_JNEW:
				drive_num = LOWORD(wParam) % 10;
				hdconf_new(hdlg, drive_num);
	                        return TRUE;
                        
                        case IDC_CFILE:
                        case IDC_DFILE:
                        case IDC_EFILE:
                        case IDC_FFILE:
                        case IDC_GFILE:
                        case IDC_HFILE:
                        case IDC_IFILE:
                        case IDC_JFILE:
				drive_num = LOWORD(wParam) % 10;
				hdconf_file(hdlg, drive_num);
	                        return TRUE;

                        case IDC_EDIT_C_SPT: case IDC_EDIT_C_HPC: case IDC_EDIT_C_CYL:
                        case IDC_EDIT_D_SPT: case IDC_EDIT_D_HPC: case IDC_EDIT_D_CYL:
                        case IDC_EDIT_E_SPT: case IDC_EDIT_E_HPC: case IDC_EDIT_E_CYL:
                        case IDC_EDIT_F_SPT: case IDC_EDIT_F_HPC: case IDC_EDIT_F_CYL:
                        case IDC_EDIT_G_SPT: case IDC_EDIT_G_HPC: case IDC_EDIT_G_CYL:
                        case IDC_EDIT_H_SPT: case IDC_EDIT_H_HPC: case IDC_EDIT_H_CYL:
                        case IDC_EDIT_I_SPT: case IDC_EDIT_I_HPC: case IDC_EDIT_I_CYL:
                        case IDC_EDIT_J_SPT: case IDC_EDIT_J_HPC: case IDC_EDIT_J_CYL:
				drive_num = LOWORD(wParam) % 10;
				hdconf_edit_boxes(hdlg, drive_num, &(hd[drive_num]));
	                        return TRUE;
                }
                break;

        }
        return FALSE;
}

void hdconf_open(HWND hwnd)
{
        if (hdd_controller_current_is_mfm())
                DialogBox(hinstance, TEXT("HdConfDlgMfm"), hwnd, hdconf_dlgproc);
        else
                DialogBox(hinstance, TEXT("HdConfDlg"), hwnd, hdconf_dlgproc);
}        
