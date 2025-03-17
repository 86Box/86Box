#include <stdint.h>
#include <86box/utils/video_stdlib.h>

/* 
    Implements a standard GDI ternary rop for e.g. bitblit acceleration.
    For further information on this function, refer to the documentation on Win32 GDI:

    https://learn.microsoft.com/en-us/windows/win32/gdi/binary-raster-operations

    This is currently used in the following graphics cards: Tseng Labs ET4000/32p, Cirrus Logic CL-GD54xx, 3dfx Voodoo Banshee/Voodoo 3, Trident TGUI9440, 
    S3 ViRGE, C&T 69000, ATI Mach64, and NVidia RIVA 128
*/
int32_t video_rop_gdi_ternary(int32_t rop, int32_t dst, int32_t pattern, int32_t src)                    
{                
    uint32_t out = 0x00;

    switch (rop) 
    {                               
        case 0x00:                             
            out = 0;                           
            break;                             
        case 0x01:                             
            out = ~(dst | (pattern | src));              
            break;                             
        case 0x02:                             
            out = dst & ~(pattern | src);                
            break;                             
        case 0x03:                             
            out = ~(pattern | src);                    
            break;                             
        case 0x04:                             
            out = src & ~(dst | pattern);                
            break;                             
        case 0x05:                             
            out = ~(dst | pattern);                    
            break;                             
        case 0x06:                             
            out = ~(pattern | ~(dst ^ src));             
            break;                             
        case 0x07:                             
            out = ~(pattern | (dst & src));              
            break;                             
        case 0x08:                             
            out = src & (dst & ~pattern);                
            break;                             
        case 0x09:                             
            out = ~(pattern | (dst ^ src));              
            break;                             
        case 0x0a:                             
            out = dst & ~pattern;                      
            break;                             
        case 0x0b:                             
            out = ~(pattern | (src & ~dst));             
            break;                             
        case 0x0c:                             
            out = src & ~pattern;                      
            break;                             
        case 0x0d:                             
            out = ~(pattern | (dst & ~src));             
            break;                             
        case 0x0e:                             
            out = ~(pattern | ~(dst | src));             
            break;                             
        case 0x0f:                             
            out = ~pattern;                          
            break;                             
        case 0x10:                             
            out = pattern & ~(dst | src);                
            break;                             
        case 0x11:                             
            out = ~(dst | src);                    
            break;                             
        case 0x12:                             
            out = ~(src | ~(dst ^ pattern));             
            break;                             
        case 0x13:                             
            out = ~(src | (dst & pattern));              
            break;                             
        case 0x14:                             
            out = ~(dst | ~(pattern ^ src));             
            break;                             
        case 0x15:                             
            out = ~(dst | (pattern & src));              
            break;                             
        case 0x16:                             
            out = pattern ^ (src ^ (dst & ~(pattern & src)));    
            break;                             
        case 0x17:                             
            out = ~(src ^ ((src ^ pattern) & (dst ^ src)));  
            break;                             
        case 0x18:                             
            out = (src ^ pattern) & (pattern ^ dst);           
            break;                             
        case 0x19:                             
            out = ~(src ^ (dst & ~(pattern & src)));       
            break;                             
        case 0x1a:                             
            out = pattern ^ (dst | (src & pattern));           
            break;                             
        case 0x1b:                             
            out = ~(src ^ (dst & (pattern ^ src)));        
            break;                             
        case 0x1c:                             
            out = pattern ^ (src | (dst & pattern));           
            break;                             
        case 0x1d:                             
            out = ~(dst ^ (src & (pattern ^ dst)));        
            break;                             
        case 0x1e:                             
            out = pattern ^ (dst | src);                 
            break;                             
        case 0x1f:                             
            out = ~(pattern & (dst | src));              
            break;                             
        case 0x20:                             
            out = dst & (pattern & ~src);                
            break;                             
        case 0x21:                             
            out = ~(src | (dst ^ pattern));              
            break;                             
        case 0x22:                             
            out = dst & ~src;                      
            break;                             
        case 0x23:                             
            out = ~(src | (pattern & ~dst));             
            break;                             
        case 0x24:                             
            out = (src ^ pattern) & (dst ^ src);           
            break;                             
        case 0x25:                             
            out = ~(pattern ^ (dst & ~(src & pattern)));       
            break;                             
        case 0x26:                             
            out = src ^ (dst | (pattern & src));           
            break;                             
        case 0x27:                             
            out = src ^ (dst | ~(pattern ^ src));          
            break;                             
        case 0x28:                             
            out = dst & (pattern ^ src);                 
            break;                             
        case 0x29:                             
            out = ~(pattern ^ (src ^ (dst | (pattern & src))));  
            break;                             
        case 0x2a:                             
            out = dst & ~(pattern & src);                
            break;                             
        case 0x2b:                             
            out = ~(src ^ ((src ^ pattern) & (pattern ^ dst)));  
            break;                             
        case 0x2c:                             
            out = src ^ (pattern & (dst | src));           
            break;                             
        case 0x2d:                             
            out = pattern ^ (src | ~dst);                
            break;                             
        case 0x2e:                             
            out = pattern ^ (src | (dst ^ pattern));           
            break;                             
        case 0x2f:                             
            out = ~(pattern & (src | ~dst));             
            break;                             
        case 0x30:                             
            out = pattern & ~src;                      
            break;                             
        case 0x31:                             
            out = ~(src | (dst & ~pattern));             
            break;                             
        case 0x32:                             
            out = src ^ (dst | (pattern | src));           
            break;                             
        case 0x33:                             
            out = ~src;                          
            break;                             
        case 0x34:                             
            out = src ^ (pattern | (dst & src));           
            break;                             
        case 0x35:                             
            out = src ^ (pattern | ~(dst ^ src));          
            break;                             
        case 0x36:                             
            out = src ^ (dst | pattern);                 
            break;                             
        case 0x37:                             
            out = ~(src & (dst | pattern));              
            break;                             
        case 0x38:                             
            out = pattern ^ (src & (dst | pattern));           
            break;                             
        case 0x39:                             
            out = src ^ (pattern | ~dst);                
            break;                             
        case 0x3a:                             
            out = src ^ (pattern | (dst ^ src));           
            break;                             
        case 0x3b:                             
            out = ~(src & (pattern | ~dst));             
            break;                             
        case 0x3c:                             
            out = pattern ^ src;                       
            break;                             
        case 0x3d:                             
            out = src ^ (pattern | ~(dst | src));          
            break;                             
        case 0x3e:                             
            out = src ^ (pattern | (dst & ~src));          
            break;                             
        case 0x3f:                             
            out = ~(pattern & src);                    
            break;                             
        case 0x40:                             
            out = pattern & (src & ~dst);                
            break;                             
        case 0x41:                             
            out = ~(dst | (pattern ^ src));              
            break;                             
        case 0x42:                             
            out = (src ^ dst) & (pattern ^ dst);           
            break;                             
        case 0x43:                             
            out = ~(src ^ (pattern & ~(dst & src)));       
            break;                             
        case 0x44:                             
            out = src & ~dst;                      
            break;                             
        case 0x45:                             
            out = ~(dst | (pattern & ~src));             
            break;                             
        case 0x46:                             
            out = dst ^ (src | (pattern & dst));           
            break;                             
        case 0x47:                             
            out = ~(pattern ^ (src & (dst ^ pattern)));        
            break;                             
        case 0x48:                             
            out = src & (dst ^ pattern);                 
            break;                             
        case 0x49:                             
            out = ~(pattern ^ (dst ^ (src | (pattern & dst))));  
            break;                             
        case 0x4a:                             
            out = dst ^ (pattern & (src | dst));           
            break;                             
        case 0x4b:                             
            out = pattern ^ (dst | ~src);                
            break;                             
        case 0x4c:                             
            out = src & ~(dst & pattern);                
            break;                             
        case 0x4d:                             
            out = ~(src ^ ((src ^ pattern) | (dst ^ src)));  
            break;                             
        case 0x4e:                             
            out = pattern ^ (dst | (src ^ pattern));           
            break;                             
        case 0x4f:                             
            out = ~(pattern & (dst | ~src));             
            break;                             
        case 0x50:                             
            out = pattern & ~dst;                      
            break;                             
        case 0x51:                             
            out = ~(dst | (src & ~pattern));             
            break;                             
        case 0x52:                             
            out = dst ^ (pattern | (src & dst));           
            break;                             
        case 0x53:                             
            out = ~(src ^ (pattern & (dst ^ src)));        
            break;                             
        case 0x54:                             
            out = ~(dst | ~(pattern | src));             
            break;                             
        case 0x55:                             
            out = ~dst;                          
            break;                             
        case 0x56:                             
            out = dst ^ (pattern | src);                 
            break;                             
        case 0x57:                             
            out = ~(dst & (pattern | src));              
            break;                             
        case 0x58:                             
            out = pattern ^ (dst & (src | pattern));           
            break;                             
        case 0x59:                             
            out = dst ^ (pattern | ~src);                
            break;                             
        case 0x5a:                             
            out = dst ^ pattern;                       
            break;                             
        case 0x5b:                             
            out = dst ^ (pattern | ~(src | dst));          
            break;                             
        case 0x5c:                             
            out = dst ^ (pattern | (src ^ dst));           
            break;                             
        case 0x5d:                             
            out = ~(dst & (pattern | ~src));             
            break;                             
        case 0x5e:                             
            out = dst ^ (pattern | (src & ~dst));          
            break;                             
        case 0x5f:                             
            out = ~(dst & pattern);                    
            break;                             
        case 0x60:                             
            out = pattern & (dst ^ src);                 
            break;                             
        case 0x61:                             
            out = ~(dst ^ (src ^ (pattern | (dst & src))));  
            break;                             
        case 0x62:                             
            out = dst ^ (src & (pattern | dst));           
            break;                             
        case 0x63:                             
            out = src ^ (dst | ~pattern);                
            break;                             
        case 0x64:                             
            out = src ^ (dst & (pattern | src));           
            break;                             
        case 0x65:                             
            out = dst ^ (src | ~pattern);                
            break;                             
        case 0x66:                             
            out = dst ^ src;                       
            break;                             
        case 0x67:                             
            out = src ^ (dst | ~(pattern | src));          
            break;                             
        case 0x68:                             
            out = ~(dst ^ (src ^ (pattern | ~(dst | src)))); 
            break;                             
        case 0x69:                             
            out = ~(pattern ^ (dst ^ src));              
            break;                             
        case 0x6a:                             
            out = dst ^ (pattern & src);                 
            break;                             
        case 0x6b:                             
            out = ~(pattern ^ (src ^ (dst & (pattern | src))));  
            break;                             
        case 0x6c:                             
            out = src ^ (dst & pattern);                 
            break;                             
        case 0x6d:                             
            out = ~(pattern ^ (dst ^ (src & (pattern | dst))));  
            break;                             
        case 0x6e:                             
            out = src ^ (dst & (pattern | ~src));          
            break;                             
        case 0x6f:                             
            out = ~(pattern & ~(dst ^ src));             
            break;                             
        case 0x70:                             
            out = pattern & ~(dst & src);                
            break;                             
        case 0x71:                             
            out = ~(src ^ ((src ^ dst) & (pattern ^ dst)));  
            break;                             
        case 0x72:                             
            out = src ^ (dst | (pattern ^ src));           
            break;                             
        case 0x73:                             
            out = ~(src & (dst | ~pattern));             
            break;                             
        case 0x74:                             
            out = dst ^ (src | (pattern ^ dst));           
            break;                             
        case 0x75:                             
            out = ~(dst & (src | ~pattern));             
            break;                             
        case 0x76:                             
            out = src ^ (dst | (pattern & ~src));          
            break;                             
        case 0x77:                             
            out = ~(dst & src);                    
            break;                             
        case 0x78:                             
            out = pattern ^ (dst & src);                 
            break;                             
        case 0x79:                             
            out = ~(dst ^ (src ^ (pattern & (dst | src))));  
            break;                             
        case 0x7a:                             
            out = dst ^ (pattern & (src | ~dst));          
            break;                             
        case 0x7b:                             
            out = ~(src & ~(dst ^ pattern));             
            break;                             
        case 0x7c:                             
            out = src ^ (pattern & (dst | ~src));          
            break;                             
        case 0x7d:                             
            out = ~(dst & ~(pattern ^ src));             
            break;                             
        case 0x7e:                             
            out = (src ^ pattern) | (dst ^ src);           
            break;                             
        case 0x7f:                             
            out = ~(dst & (pattern & src));              
            break;                             
        case 0x80:                             
            out = dst & (pattern & src);                 
            break;                             
        case 0x81:                             
            out = ~((src ^ pattern) | (dst ^ src));        
            break;                             
        case 0x82:                             
            out = dst & ~(pattern ^ src);                
            break;                             
        case 0x83:                             
            out = ~(src ^ (pattern & (dst | ~src)));       
            break;                             
        case 0x84:                             
            out = src & ~(dst ^ pattern);                
            break;                             
        case 0x85:                             
            out = ~(pattern ^ (dst & (src | ~pattern)));       
            break;                             
        case 0x86:                             
            out = dst ^ (src ^ (pattern & (dst | src)));     
            break;                             
        case 0x87:                             
            out = ~(pattern ^ (dst & src));              
            break;                             
        case 0x88:                             
            out = dst & src;                       
            break;                             
        case 0x89:                             
            out = ~(src ^ (dst | (pattern & ~src)));       
            break;                             
        case 0x8a:                             
            out = dst & (src | ~pattern);                
            break;                             
        case 0x8b:                             
            out = ~(dst ^ (src | (pattern ^ dst)));        
            break;                             
        case 0x8c:                             
            out = src & (dst | ~pattern);                
            break;                             
        case 0x8d:                             
            out = ~(src ^ (dst | (pattern ^ src)));        
            break;                             
        case 0x8e:                             
            out = src ^ ((src ^ dst) & (pattern ^ dst));     
            break;                             
        case 0x8f:                             
            out = ~(pattern & ~(dst & src));             
            break;                             
        case 0x90:                             
            out = pattern & ~(dst ^ src);                
            break;                             
        case 0x91:                             
            out = ~(src ^ (dst & (pattern | ~src)));       
            break;                             
        case 0x92:                             
            out = dst ^ (pattern ^ (src & (dst | pattern)));     
            break;                             
        case 0x93:                             
            out = ~(src ^ (pattern & dst));              
            break;                             
        case 0x94:                             
            out = pattern ^ (src ^ (dst & (pattern | src)));     
            break;                             
        case 0x95:                             
            out = ~(dst ^ (pattern & src));              
            break;                             
        case 0x96:                             
            out = dst ^ (pattern ^ src);                 
            break;                             
        case 0x97:                             
            out = pattern ^ (src ^ (dst | ~(pattern | src)));    
            break;                             
        case 0x98:                             
            out = ~(src ^ (dst | ~(pattern | src)));       
            break;                             
        case 0x99:                             
            out = ~(dst ^ src);                    
            break;                             
        case 0x9a:                             
            out = dst ^ (pattern & ~src);                
            break;                             
        case 0x9b:                             
            out = ~(src ^ (dst & (pattern | src)));        
            break;                             
        case 0x9c:                             
            out = src ^ (pattern & ~dst);                
            break;                             
        case 0x9d:                             
            out = ~(dst ^ (src & (pattern | dst)));        
            break;                             
        case 0x9e:                             
            out = dst ^ (src ^ (pattern | (dst & src)));     
            break;                             
        case 0x9f:                             
            out = ~(pattern & (dst ^ src));              
            break;                             
        case 0xa0:                             
            out = dst & pattern;                       
            break;                             
        case 0xa1:                             
            out = ~(pattern ^ (dst | (src & ~pattern)));       
            break;                             
        case 0xa2:                             
            out = dst & (pattern | ~src);                
            break;                             
        case 0xa3:                             
            out = ~(dst ^ (pattern | (src ^ dst)));        
            break;                             
        case 0xa4:                             
            out = ~(pattern ^ (dst | ~(src | pattern)));       
            break;                             
        case 0xa5:                             
            out = ~(pattern ^ dst);                    
            break;                             
        case 0xa6:                             
            out = dst ^ (src & ~pattern);                
            break;                             
        case 0xa7:                             
            out = ~(pattern ^ (dst & (src | pattern)));        
            break;                             
        case 0xa8:                             
            out = dst & (pattern | src);                 
            break;                             
        case 0xa9:                             
            out = ~(dst ^ (pattern | src));              
            break;                             
        case 0xaa:                             
            out = dst;                           
            break;                             
        case 0xab:                             
            out = dst | ~(pattern | src);                
            break;                             
        case 0xac:                             
            out = src ^ (pattern & (dst ^ src));           
            break;                             
        case 0xad:                             
            out = ~(dst ^ (pattern | (src & dst)));        
            break;                             
        case 0xae:                             
            out = dst | (src & ~pattern);                
            break;                             
        case 0xaf:                             
            out = dst | ~pattern;                      
            break;                             
        case 0xb0:                             
            out = pattern & (dst | ~src);                
            break;                             
        case 0xb1:                             
            out = ~(pattern ^ (dst | (src ^ pattern)));        
            break;                             
        case 0xb2:                             
            out = src ^ ((src ^ pattern) | (dst ^ src));     
            break;                             
        case 0xb3:                             
            out = ~(src & ~(dst & pattern));             
            break;                             
        case 0xb4:                             
            out = pattern ^ (src & ~dst);                
            break;                             
        case 0xb5:                             
            out = ~(dst ^ (pattern & (src | dst)));        
            break;                             
        case 0xb6:                             
            out = dst ^ (pattern ^ (src | (dst & pattern)));     
            break;                             
        case 0xb7:                             
            out = ~(src & (dst ^ pattern));              
            break;                             
        case 0xb8:                             
            out = pattern ^ (src & (dst ^ pattern));           
            break;                             
        case 0xb9:                             
            out = ~(dst ^ (src | (pattern & dst)));        
            break;                             
        case 0xba:                             
            out = dst | (pattern & ~src);                
            break;                             
        case 0xbb:                             
            out = dst | ~src;                      
            break;                             
        case 0xbc:                             
            out = src ^ (pattern & ~(dst & src));          
            break;                             
        case 0xbd:                             
            out = ~((src ^ dst) & (pattern ^ dst));        
            break;                             
        case 0xbe:                             
            out = dst | (pattern ^ src);                 
            break;                             
        case 0xbf:                             
            out = dst | ~(pattern & src);                
            break;                             
        case 0xc0:                             
            out = pattern & src;                       
            break;                             
        case 0xc1:                             
            out = ~(src ^ (pattern | (dst & ~src)));       
            break;                             
        case 0xc2:                             
            out = ~(src ^ (pattern | ~(dst | src)));       
            break;                             
        case 0xc3:                             
            out = ~(pattern ^ src);                    
            break;                             
        case 0xc4:                             
            out = src & (pattern | ~dst);                
            break;                             
        case 0xc5:                             
            out = ~(src ^ (pattern | (dst ^ src)));        
            break;                             
        case 0xc6:                             
            out = src ^ (dst & ~pattern);                
            break;                             
        case 0xc7:                             
            out = ~(pattern ^ (src & (dst | pattern)));        
            break;                             
        case 0xc8:                             
            out = src & (dst | pattern);                 
            break;                             
        case 0xc9:                             
            out = ~(src ^ (pattern | dst));              
            break;                             
        case 0xca:                             
            out = dst ^ (pattern & (src ^ dst));           
            break;                             
        case 0xcb:                             
            out = ~(src ^ (pattern | (dst & src)));        
            break;                             
        case 0xcc:                             
            out = src;                           
            break;                             
        case 0xcd:                             
            out = src | ~(dst | pattern);                
            break;                             
        case 0xce:                             
            out = src | (dst & ~pattern);                
            break;                             
        case 0xcf:                             
            out = src | ~pattern;                      
            break;                             
        case 0xd0:                             
            out = pattern & (src | ~dst);                
            break;                             
        case 0xd1:                             
            out = ~(pattern ^ (src | (dst ^ pattern)));        
            break;                             
        case 0xd2:                             
            out = pattern ^ (dst & ~src);                
            break;                             
        case 0xd3:                             
            out = ~(src ^ (pattern & (dst | src)));        
            break;                             
        case 0xd4:                             
            out = src ^ ((src ^ pattern) & (pattern ^ dst));     
            break;                             
        case 0xd5:                             
            out = ~(dst & ~(pattern & src));             
            break;                             
        case 0xd6:                             
            out = pattern ^ (src ^ (dst | (pattern & src)));     
            break;                             
        case 0xd7:                             
            out = ~(dst & (pattern ^ src));              
            break;                             
        case 0xd8:                             
            out = pattern ^ (dst & (src ^ pattern));           
            break;                             
        case 0xd9:                             
            out = ~(src ^ (dst | (pattern & src)));        
            break;                             
        case 0xda:                             
            out = dst ^ (pattern & ~(src & dst));          
            break;                             
        case 0xdb:                             
            out = ~((src ^ pattern) & (dst ^ src));        
            break;                             
        case 0xdc:                             
            out = src | (pattern & ~dst);                
            break;                             
        case 0xdd:                             
            out = src | ~dst;                      
            break;                             
        case 0xde:                             
            out = src | (dst ^ pattern);                 
            break;                             
        case 0xdf:                             
            out = src | ~(dst & pattern);                
            break;                             
        case 0xe0:                             
            out = pattern & (dst | src);                 
            break;                             
        case 0xe1:                             
            out = ~(pattern ^ (dst | src));              
            break;                             
        case 0xe2:                             
            out = dst ^ (src & (pattern ^ dst));           
            break;                             
        case 0xe3:                             
            out = ~(pattern ^ (src | (dst & pattern)));        
            break;                             
        case 0xe4:                             
            out = src ^ (dst & (pattern ^ src));           
            break;                             
        case 0xe5:                             
            out = ~(pattern ^ (dst | (src & pattern)));        
            break;                             
        case 0xe6:                             
            out = src ^ (dst & ~(pattern & src));          
            break;                             
        case 0xe7:                             
            out = ~((src ^ pattern) & (pattern ^ dst));        
            break;                             
        case 0xe8:                             
            out = src ^ ((src ^ pattern) & (dst ^ src));     
            break;                             
        case 0xe9:                             
            out = ~(dst ^ (src ^ (pattern & ~(dst & src)))); 
            break;                             
        case 0xea:                             
            out = dst | (pattern & src);                 
            break;                             
        case 0xeb:                             
            out = dst | ~(pattern ^ src);                
            break;                             
        case 0xec:                             
            out = src | (dst & pattern);                 
            break;                             
        case 0xed:                             
            out = src | ~(dst ^ pattern);                
            break;                             
        case 0xee:                             
            out = dst | src;                       
            break;                             
        case 0xef:                             
            out = src | (dst | ~pattern);                
            break;                             
        case 0xf0:                             
            out = pattern;                           
            break;                             
        case 0xf1:                             
            out = pattern | ~(dst | src);                
            break;                             
        case 0xf2:                             
            out = pattern | (dst & ~src);                
            break;                             
        case 0xf3:                             
            out = pattern | ~src;                      
            break;                             
        case 0xf4:                             
            out = pattern | (src & ~dst);                
            break;                             
        case 0xf5:                             
            out = pattern | ~dst;                      
            break;                             
        case 0xf6:                             
            out = pattern | (dst ^ src);                 
            break;                             
        case 0xf7:                             
            out = pattern | ~(dst & src);                
            break;                             
        case 0xf8:                             
            out = pattern | (dst & src);                 
            break;                             
        case 0xf9:                             
            out = pattern | ~(dst ^ src);                
            break;                             
        case 0xfa:                             
            out = dst | pattern;                       
            break;                             
        case 0xfb:                             
            out = dst | (pattern | ~src);                
            break;                             
        case 0xfc:                             
            out = pattern | src;                       
            break;                             
        case 0xfd:                             
            out = pattern | (src | ~dst);                
            break;                             
        case 0xfe:                             
            out = dst | (pattern | src);                 
            break;                             
        case 0xff:                             
            out = ~0;                          
            break;                             
    }           
    
    return out; 
}