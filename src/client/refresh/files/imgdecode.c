/* stub for the red phase */
#include "imgdecode.h"

imgdec_type_t IMGDEC_TypeOfExtension(const char *ext) { (void)ext; return IMGDEC_TYPE_NONE; }
imgdec_type_t IMGDEC_TypeOfIdentifier(const char *uti) { (void)uti; return IMGDEC_TYPE_NONE; }
int IMGDEC_PixelBytes(size_t width, size_t height, size_t max_side, size_t *bytes) { (void)width; (void)height; (void)max_side; (void)bytes; return 0; }
imgdec_result_t IMGDEC_ToRGBA(const imgdec_layout_t *layout, const unsigned char *src, size_t srclen, unsigned char *dst) { (void)layout; (void)src; (void)srclen; (void)dst; return IMGDEC_ERR_UNSUPPORTED; }
imgdec_result_t IMGDEC_Decode(const void *data, size_t size, const char *ext, unsigned char **pixels, int *width, int *height) { (void)data; (void)size; (void)ext; (void)width; (void)height; *pixels = 0; return IMGDEC_ERR_UNSUPPORTED; }
const char *IMGDEC_ResultString(imgdec_result_t result) { (void)result; return "stub"; }
