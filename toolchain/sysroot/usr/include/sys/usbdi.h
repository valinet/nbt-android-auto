/* Open QNX 6.5.0 USB stack client API (libusbdi.so.2), the subset this project uses.
 * Layouts from the public QNX USB DDK reference; sizes and constants checked against oneplus_native
 * as built with the original SDP (connect parm = 48 bytes, funcs = 16, instance config at 0x18). */
#ifndef _SYS_USBDI_H_INCLUDED
#define _SYS_USBDI_H_INCLUDED
#include <_qnx_open.h>
#include <stdint.h>
#include <stddef.h>
__BEGIN_DECLS
struct usbd_connection;
struct usbd_device;
struct usbd_pipe;
struct usbd_urb;
struct usbd_desc_node;

#define USB_VERSION  0x0110
#define USBD_VERSION 0x0101
#define USBD_CONNECT_WILDCARD 0xffffffff
#define USBD_CONNECT_WAIT 0xffff
#define _USBDI_NFUNCS 3

typedef struct usbd_device_ident {
  uint32_t vendor, device, dclass, subclass, protocol;
} usbd_device_ident_t;

typedef struct usbd_device_instance {
  uint8_t path;
  uint8_t devno;
  uint16_t generation;
  usbd_device_ident_t ident;
  uint32_t config, iface, alternate;
} usbd_device_instance_t;

typedef struct usbd_funcs {
  uint32_t nentries;
  void (*insertion)(struct usbd_connection *, usbd_device_instance_t *);
  void (*removal)(struct usbd_connection *, usbd_device_instance_t *);
  void (*event)(struct usbd_connection *, usbd_device_instance_t *, uint16_t);
} usbd_funcs_t;

typedef struct usbd_connect_parm {
  const char *path;
  uint16_t vusb;
  uint16_t vusbd;
  uint32_t flags;
  int argc;
  char **argv;
  uint32_t evtbufsz;
  usbd_device_ident_t *ident;
  usbd_funcs_t *funcs;
  uint16_t connect_wait;
  uint8_t __reserved[14];
} usbd_connect_parm_t;

/* Standard USB descriptors (packed, as on the wire) */
typedef struct usbd_descriptor_header { uint8_t bLength, bDescriptorType; } __attribute__((packed)) usbd_descriptor_header_t;
typedef struct usbd_device_descriptor {
  uint8_t bLength, bDescriptorType; uint16_t bcdUSB; uint8_t bDeviceClass, bDeviceSubClass, bDeviceProtocol, bMaxPacketSize0;
  uint16_t idVendor, idProduct, bcdDevice; uint8_t iManufacturer, iProduct, iSerialNumber, bNumConfigurations;
} __attribute__((packed)) usbd_device_descriptor_t;
typedef struct usbd_configuration_descriptor {
  uint8_t bLength, bDescriptorType; uint16_t wTotalLength; uint8_t bNumInterfaces, bConfigurationValue, iConfiguration, bmAttributes, MaxPower;
} __attribute__((packed)) usbd_configuration_descriptor_t;
typedef struct usbd_interface_descriptor {
  uint8_t bLength, bDescriptorType, bInterfaceNumber, bAlternateSetting, bNumEndpoints, bInterfaceClass,
          bInterfaceSubClass, bInterfaceProtocol, iInterface;
} __attribute__((packed)) usbd_interface_descriptor_t;
typedef struct usbd_endpoint_descriptor {
  uint8_t bLength, bDescriptorType, bEndpointAddress, bmAttributes; uint16_t wMaxPacketSize; uint8_t bInterval;
} __attribute__((packed)) usbd_endpoint_descriptor_t;
typedef union usbd_descriptors {
  usbd_descriptor_header_t generic;
  usbd_device_descriptor_t device;
  usbd_configuration_descriptor_t configuration;
  usbd_interface_descriptor_t interface;
  usbd_endpoint_descriptor_t endpoint;
} usbd_descriptors_t;

#define USB_DESC_DEVICE 0x01
#define USB_DESC_CONFIGURATION 0x02
#define USB_DESC_STRING 0x03
#define USB_DESC_INTERFACE 0x04
#define USB_DESC_ENDPOINT 0x05
#define USB_ATTRIB_CONTROL 0x00
#define USB_ATTRIB_ISOCHRONOUS 0x01
#define USB_ATTRIB_BULK 0x02
#define USB_ATTRIB_INTERRUPT 0x03
#define USB_ENDPOINT_IN 0x80
#define USB_ENDPOINT_OUT 0x00
#define USB_DIRECTION_HOST 0x80
#define USB_DIRECTION_DEVICE 0x00
#define USB_TYPE_STANDARD 0x00
#define USB_TYPE_CLASS 0x20
#define USB_TYPE_VENDOR 0x40
#define USB_RECIPIENT_DEVICE 0x00
#define USB_RECIPIENT_INTERFACE 0x01
#define USB_RECIPIENT_ENDPOINT 0x02
#define USB_RECIPIENT_OTHER 0x03

#define URB_DIR_IN 0x01
#define URB_DIR_OUT 0x02
#define URB_DIR_NONE 0x03
#define URB_SHORT_XFER_OK 0x04
#define USBD_URB_STATUS_MASK 0xff000000
#define USBD_STATUS_CMP 0x01000000

int usbd_connect(usbd_connect_parm_t *, struct usbd_connection **);
int usbd_disconnect(struct usbd_connection *);
int usbd_attach(struct usbd_connection *, usbd_device_instance_t *, size_t, struct usbd_device **);
int usbd_detach(struct usbd_device *);
usbd_descriptors_t *usbd_parse_descriptors(struct usbd_device *, struct usbd_desc_node *, uint8_t, int, struct usbd_desc_node **);
usbd_interface_descriptor_t *usbd_interface_descriptor(struct usbd_device *, uint8_t, uint8_t, uint8_t, struct usbd_desc_node **);
int usbd_open_pipe(struct usbd_device *, usbd_descriptors_t *, struct usbd_pipe **);
int usbd_close_pipe(struct usbd_pipe *);
int usbd_abort_pipe(struct usbd_pipe *);
struct usbd_urb *usbd_alloc_urb(struct usbd_urb *);
struct usbd_urb *usbd_free_urb(struct usbd_urb *);
void *usbd_alloc(size_t);
void usbd_free(void *);
int usbd_setup_bulk(struct usbd_urb *, uint32_t, void *, uint32_t);
int usbd_setup_vendor(struct usbd_urb *, uint32_t, uint16_t, uint16_t, uint16_t, uint16_t, void *, uint32_t);
int usbd_io(struct usbd_urb *, struct usbd_pipe *, void (*)(struct usbd_urb *, struct usbd_pipe *, void *), void *, uint32_t);
int usbd_urb_status(struct usbd_urb *, uint32_t *, uint32_t *);
__END_DECLS
#endif
