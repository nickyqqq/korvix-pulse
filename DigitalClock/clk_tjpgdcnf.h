/* Private copy of ChaN's TJpgDec R0.03 (via Bodmer's TJpg_Decoder), renamed so it can
   live next to the library, configured for maximum speed (JD_FASTDECODE 2). Used for GIF frames. */
/*----------------------------------------------*/
/* TJpgDec System Configurations R0.03          */
/*----------------------------------------------*/

#define	CLK_JD_SZBUF		512
/* Specifies size of stream input buffer */

#define CLK_JD_FORMAT		1
/* Specifies output pixel format.
/  0: RGB888 (24-bit/pix)
/  1: RGB565 (16-bit/pix)
/  2: Grayscale (8-bit/pix)
*/

#define	CLK_JD_USE_SCALE	0
/* Switches output descaling feature.
/  0: Disable
/  1: Enable
*/

#define CLK_JD_TBLCLIP		1
/* Use table conversion for saturation arithmetic. A bit faster, but increases 1 KB of code size.
/  0: Disable
/  1: Enable
*/

#define CLK_JD_FASTDECODE	2
/* Optimization level
/  0: Basic optimization. Suitable for 8/16-bit MCUs.
/     Workspace of 3100 bytes needed.
/  1: + 32-bit barrel shifter. Suitable for 32-bit MCUs.
/     Workspace of 3480 bytes needed.
/  2: + Table conversion for huffman decoding (wants 6 << HUFF_BIT bytes of RAM).
/     Workspace of 9644 bytes needed.
*/

// Workspace: CustomAnimation gives it 11 KB (9644 bytes for level 2 + 1 KB clip table).
