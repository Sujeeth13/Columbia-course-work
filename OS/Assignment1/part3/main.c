#include "../part3_include/efi.h"
#include "../part3_include/efilib.h"
#include "../part3_include/font8x8_basic.h"
#define CHAR_SPACING 8

EFI_PHYSICAL_ADDRESS PIXEL_BUFFER_BASE;
UINT32 HEIGHT, WIDTH;

void draw_char(EFI_GRAPHICS_OUTPUT_BLT_PIXEL *buffer, char ch, UINTN x, UINTN y) {
    UINT8 *font_data = font8x8_basic[(int)ch];
    for (UINTN i = 0; i < 8; i++) {
        for (UINTN j = 0; j < 8; j++) {
            if (font_data[i] & (1 << j)) {
                // Assuming the text is white and background is black
                UINTN pixel_index = (j + x) + (i + y)*WIDTH;
                buffer[pixel_index].Red = 0xFF;
                buffer[pixel_index].Green = 0xFF;
                buffer[pixel_index].Blue = 0xFF;
            }
        }
    }
}

EFI_STATUS
EFIAPI
efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE * SystemTable)
{
	EFI_STATUS status;
	EFI_GUID gEfiGraphicsOutputProtocolGuid =
		EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
	EFI_GRAPHICS_OUTPUT_PROTOCOL *gop;
	EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *info;
	UINTN sizeOfInfo;
	UINT32 pixelPages;

	status = SystemTable->BootServices->LocateProtocol(
		&gEfiGraphicsOutputProtocolGuid, NULL, (void **)&gop
	);
	if (EFI_ERROR(status))
		return status;

	status = gop->SetMode(gop, 0);
	if (EFI_ERROR(status))
		return status;

	status = gop->QueryMode(gop, 0, &sizeOfInfo, &info);
	if (EFI_ERROR(status))
		return status;

	HEIGHT = info->VerticalResolution;
	WIDTH = info->HorizontalResolution;
	pixelPages = (
		HEIGHT * WIDTH * sizeof(EFI_GRAPHICS_OUTPUT_BLT_PIXEL)
	) / 4096 + 1;

	status = SystemTable->BootServices->AllocatePages(
		AllocateAnyPages, EfiLoaderData, pixelPages, &PIXEL_BUFFER_BASE
	);
	if (EFI_ERROR(status))
		return status;

	// WRITE YOUR CODE BELOW THIS LINE
	for (UINTN i = 0; i < WIDTH * HEIGHT; i++) {
    	((EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)PIXEL_BUFFER_BASE)[i].Red = 0;
    	((EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)PIXEL_BUFFER_BASE)[i].Green = 0;
    	((EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)PIXEL_BUFFER_BASE)[i].Blue = 0;
	}

	char* message = "hello, world";
	UINTN x = 0;
	UINTN y = 0;
	for(UINTN i=0;i<13;i++){
		draw_char((EFI_GRAPHICS_OUTPUT_BLT_PIXEL *)PIXEL_BUFFER_BASE, message[i], x + i * CHAR_SPACING, y);
	}


	status = gop->Blt(
		gop, (void *)PIXEL_BUFFER_BASE, EfiBltBufferToVideo,
		0, 0, 0, 0, WIDTH, HEIGHT, 0
	);
	while(1);
	if (EFI_ERROR(status))
		return status;
}
