/* Copyright holders: Sarah Walker
   see COPYING for more details
*/
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include "config.h"
#include "ibm.h"

wchar_t config_file_default[256];

static wchar_t config_file[256];

typedef struct list_t
{
        struct list_t *next;
} list_t;

static list_t config_head;

typedef struct section_t
{
        struct list_t list;
        
        char name[256];
        
        struct list_t entry_head;
} section_t;

typedef struct entry_t
{
        struct list_t list;
        
        char name[256];
        char data[256];
        wchar_t wdata[256];
} entry_t;

#define list_add(new, head)                             \
        {                                               \
                struct list_t *next = head;             \
                                                        \
                while (next->next)                      \
                        next = next->next;              \
                                                        \
                (next)->next = new;                     \
                (new)->next = NULL;                     \
        }

void config_dump()
{
        section_t *current_section;
        
        pclog("Config data :\n");
        
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                entry_t *current_entry;
                
                pclog("[%s]\n", current_section->name);
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
                        pclog("%s = %s\n", current_entry->name, current_entry->data);

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
}

void config_free()
{
        section_t *current_section;
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                section_t *next_section = (section_t *)current_section->list.next;
                entry_t *current_entry;
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
                        entry_t *next_entry = (entry_t *)current_entry->list.next;
                        
                        free(current_entry);
                        current_entry = next_entry;
                }

                free(current_section);                
                current_section = next_section;
        }
}

void config_load(wchar_t *fn)
{
        FILE *f = _wfopen(fn, L"rt, ccs=UNICODE");
        section_t *current_section;
        
        memset(&config_head, 0, sizeof(list_t));

        current_section = malloc(sizeof(section_t));
        memset(current_section, 0, sizeof(section_t));
        list_add(&current_section->list, &config_head);

        if (!f)
                return;

        while (1)
        {
                int c;
                wchar_t buffer[1024];
		int org_pos;

		memset(buffer, 0, 2048);
                fgetws(buffer, 255, f);
                if (feof(f)) break;
                
                c = 0;
                
                while (buffer[c] == L' ')
                      c++;

                if (buffer[c] == L'\0') continue;
                
                if (buffer[c] == L'#') /*Comment*/
                        continue;

                if (buffer[c] == L'[') /*Section*/
                {
                        section_t *new_section;
                        char name[256];
                        int d = 0;
                        
                        c++;
                        while (buffer[c] != L']' && buffer[c])
                                wctomb(&(name[d++]), buffer[c++]);

                        if (buffer[c] != L']')
                                continue;
                        name[d] = 0;
                        
                        new_section = malloc(sizeof(section_t));
                        memset(new_section, 0, sizeof(section_t));
                        strncpy(new_section->name, name, 256);
                        list_add(&new_section->list, &config_head);
                        
                        current_section = new_section;                        
                }
                else
                {
                        entry_t *new_entry;
                        char name[256];
                        int d = 0, data_pos;

                        while (buffer[c] != L'=' && buffer[c] != L' ' && buffer[c])
                                wctomb(&(name[d++]), buffer[c++]);
                
                        if (buffer[c] == L'\0') continue;
                        name[d] = 0;

                        while ((buffer[c] == L'=' || buffer[c] == L' ') && buffer[c])
                                c++;
                        
                        if (!buffer[c]) continue;
                        
                        data_pos = c;
                        while (buffer[c])
                        {
                                if (buffer[c] == L'\n')
                                        buffer[c] = L'\0';
                                c++;
                        }

                        new_entry = malloc(sizeof(entry_t));
                        memset(new_entry, 0, sizeof(entry_t));
                        strncpy(new_entry->name, name, 256);
			memcpy(new_entry->wdata, &buffer[data_pos], 512);
			new_entry->wdata[255] = L'\0';
			wcstombs(new_entry->data, new_entry->wdata, 512);
			new_entry->data[255] = '\0';
                        list_add(&new_entry->list, &current_section->entry_head);
                }
        }
        
        fclose(f);
        
        config_dump();
}



void config_new()
{
        FILE *f = _wfopen(config_file, L"wt, ccs=UNICODE");
        fclose(f);
}

static section_t *find_section(char *name)
{
        section_t *current_section;
        char blank[] = "";
        
        current_section = (section_t *)config_head.next;
        if (!name)
                name = blank;

        while (current_section)
        {
                if (!strncmp(current_section->name, name, 256))
                        return current_section;
                
                current_section = (section_t *)current_section->list.next;
        }
        return NULL;
}

static entry_t *find_entry(section_t *section, char *name)
{
        entry_t *current_entry;
        
        current_entry = (entry_t *)section->entry_head.next;
        
        while (current_entry)
        {
                if (!strncmp(current_entry->name, name, 256))
                        return current_entry;

                current_entry = (entry_t *)current_entry->list.next;
        }
        return NULL;
}

static section_t *create_section(char *name)
{
        section_t *new_section = malloc(sizeof(section_t));

        memset(new_section, 0, sizeof(section_t));
        strncpy(new_section->name, name, 256);
        list_add(&new_section->list, &config_head);
        
        return new_section;
}

static entry_t *create_entry(section_t *section, char *name)
{
        entry_t *new_entry = malloc(sizeof(entry_t));
        memset(new_entry, 0, sizeof(entry_t));
        strncpy(new_entry->name, name, 256);
        list_add(&new_entry->list, &section->entry_head);
        
        return new_entry;
}
        
int config_get_int(char *head, char *name, int def)
{
        section_t *section;
        entry_t *entry;
        int value;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
        
        sscanf(entry->data, "%i", &value);
        
        return value;
}

char *config_get_string(char *head, char *name, char *def)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
       
        return entry->data; 
}

wchar_t *config_get_wstring(char *head, char *name, wchar_t *def)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                return def;
                
        entry = find_entry(section, name);

        if (!entry)
                return def;
       
        return entry->wdata; 
}

void config_set_int(char *head, char *name, int val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        sprintf(entry->data, "%i", val);
	mbstowcs(entry->wdata, entry->data, 512);
}

void config_set_string(char *head, char *name, char *val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        strncpy(entry->data, val, 256);
	mbstowcs(entry->wdata, entry->data, 256);
}

void config_set_wstring(char *head, char *name, wchar_t *val)
{
        section_t *section;
        entry_t *entry;

        section = find_section(head);
        
        if (!section)
                section = create_section(head);
                
        entry = find_entry(section, name);

        if (!entry)
                entry = create_entry(section, name);

        memcpy(entry->wdata, val, 512);
}


char *get_filename(char *s)
{
        int c = strlen(s) - 1;
        while (c > 0)
        {
                if (s[c] == '/' || s[c] == '\\')
                   return &s[c+1];
               c--;
        }
        return s;
}

wchar_t *get_filename_w(wchar_t *s)
{
        int c = wcslen(s) - 1;
        while (c > 0)
        {
                if (s[c] == L'/' || s[c] == L'\\')
                   return &s[c+1];
               c--;
        }
        return s;
}

void append_filename(char *dest, char *s1, char *s2, int size)
{
        sprintf(dest, "%s%s", s1, s2);
}

void append_filename_w(wchar_t *dest, wchar_t *s1, wchar_t *s2, int size)
{
        _swprintf(dest, L"%s%s", s1, s2);
}

void put_backslash(char *s)
{
        int c = strlen(s) - 1;
        if (s[c] != '/' && s[c] != '\\')
           s[c] = '/';
}

void put_backslash_w(wchar_t *s)
{
        int c = wcslen(s) - 1;
        if (s[c] != L'/' && s[c] != L'\\')
           s[c] = L'/';
}

char *get_extension(char *s)
{
        int c = strlen(s) - 1;

        if (c <= 0)
                return s;
        
        while (c && s[c] != '.')
                c--;
                
        if (!c)
                return &s[strlen(s)];

        return &s[c+1];
}               

wchar_t *get_extension_w(wchar_t *s)
{
        int c = wcslen(s) - 1;

        if (c <= 0)
                return s;
        
        while (c && s[c] != L'.')
                c--;
                
        if (!c)
                return &s[wcslen(s)];

        return &s[c+1];
}               

static wchar_t wname[512];

void config_save(wchar_t *fn)
{
        FILE *f = _wfopen(fn, L"wt, ccs=UNICODE");
        section_t *current_section;
        
        current_section = (section_t *)config_head.next;
        
        while (current_section)
        {
                entry_t *current_entry;
                
                if (current_section->name[0])
		{
			mbstowcs(wname, current_section->name, strlen(current_section->name) + 1);
                        _fwprintf_p(f, L"\n[%ws]\n", wname);
		}
                
                current_entry = (entry_t *)current_section->entry_head.next;
                
                while (current_entry)
                {
			mbstowcs(wname, current_entry->name, strlen(current_entry->name) + 1);
			if (current_entry->wdata[0] == L'\0')
			{
	                        _fwprintf_p(f, L"%ws = \n", wname);
			}
			else
			{
	                        _fwprintf_p(f, L"%ws = %ws\n", wname, current_entry->wdata);
			}

                        current_entry = (entry_t *)current_entry->list.next;
                }

                current_section = (section_t *)current_section->list.next;
        }
        
        fclose(f);
}
