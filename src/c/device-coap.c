/* Entry point for an EdgeX device service that provides a CoAP server to receive
 * values asynchronously.
 *
 * Copyright (c) 2020
 * Ken Bannister
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <unistd.h>
#include <stdarg.h>

#include "devsdk/devsdk.h"
#include "device-coap.h"

#define ERR_CHECK(x) if (x.code) { fprintf (stderr, "Error: %d: %s\n", x.code, x.reason); devsdk_service_free (service); free (impl); return x.code; }

#define COAP_BIND_ADDR_KEY "CoapBindAddr"
#define SECURITY_MODE_KEY  "SecurityMode"
#define PSK_KEY_KEY        "PskKey"
#define NOT_SUPPORTED_TEXT "Request not supported; CoAP devices are push-only"


/* Looks up security mode enum value from configuration text value */
static coap_security_mode_t find_security_mode
(
  const char *mode_text
)
{
  assert (mode_text);
  if (!strcmp (mode_text, "PSK"))
  {
    return SECURITY_MODE_PSK;
  }
  else if (!strcmp (mode_text, "NoSec"))
  {
    return SECURITY_MODE_NOSEC;
  }
  else
  {
    return SECURITY_MODE_UNKNOWN;
  }
}

/* Init callback; reads in config values to device driver */
static bool coap_init
(
  void *impl,
  struct iot_logger_t *lc,
  const iot_data_t *config
)
{
  coap_driver *driver = (coap_driver *) impl;

  driver->lc = lc;
  driver->security_mode = find_security_mode (iot_data_string_map_get_string (config, SECURITY_MODE_KEY));

  switch (driver->security_mode) {
    case SECURITY_MODE_UNKNOWN:
      iot_log_error (lc, "Unknown security mode");
      driver->psk_key = NULL;
      return false;

    case SECURITY_MODE_PSK:
    {
      const char *conf_psk_key = iot_data_string_map_get_string (config, PSK_KEY_KEY);
      if (strlen (conf_psk_key))
      {
        iot_data_t *key_array = iot_data_alloc_array_from_base64 (conf_psk_key);
        driver->psk_key = key_array;
        iot_log_info (lc, "PSK key len %u", iot_data_array_length (key_array));
      }
      else
      {
        iot_log_error (lc, "PSK key not in configuration");
        driver->psk_key = NULL;
        return false;
      }
      break;
    }
    default:
      driver->psk_key = NULL;
  }

  /* CoAP server bind address as text */
  const char *bind_addr = iot_data_string_map_get_string (config, COAP_BIND_ADDR_KEY);
  if (bind_addr)
  {
    driver->coap_bind_addr = iot_data_alloc_string (bind_addr, IOT_DATA_COPY);
  }
  else
  {
    iot_log_error (lc, "CoAP bind address not in configuration");
    driver->coap_bind_addr = NULL;
    return false;
  }

  iot_log_debug (lc, "Init complete");
  return true;
}

static bool coap_get_handler
(
  void *impl,
  const devsdk_device_t *device,
  uint32_t nreadings,
  const devsdk_commandrequest *requests,
  devsdk_commandresult *readings,
  const iot_data_t *options,
  iot_data_t **exception
)
{
  (void) impl;
  (void) device;
  (void) nreadings;
  (void) requests;
  (void) readings;
  (void) options;

  *exception = iot_data_alloc_string (NOT_SUPPORTED_TEXT, IOT_DATA_REF);
  return false;
}

static bool coap_put_handler
(
  void *impl,
  const devsdk_device_t *device,
  uint32_t nvalues,
  const devsdk_commandrequest *requests,
  const iot_data_t *values[],
  const iot_data_t *options,
  iot_data_t **exception
)
{
  (void) impl;
  (void) device;
  (void) nvalues;
  (void) requests;
  (void) values;
  (void) options;

  *exception = iot_data_alloc_string (NOT_SUPPORTED_TEXT, IOT_DATA_REF);
  return false;
}

static void coap_stop (void *impl, bool force) {}

static devsdk_address_t coap_create_address (void *impl, const devsdk_protocols *protocols, iot_data_t **exception)
{
  return (devsdk_address_t)protocols;
}

static void coap_free_address (void *impl, devsdk_address_t address)
{
}

static devsdk_resource_attr_t coap_create_resource_attr (void *impl, const iot_data_t *attributes, iot_data_t **exception)
{
  return (devsdk_resource_attr_t)attributes;
}

static void coap_free_resource_attr (void *impl, devsdk_resource_attr_t attr)
{
}

int main (int argc, char *argv[])
{
  coap_driver * impl = malloc (sizeof (coap_driver));
  memset (impl, 0, sizeof (coap_driver));

  devsdk_error e;
  e.code = 0;

  /* Device Callbacks */
  devsdk_callbacks *coapImpls = devsdk_callbacks_init
  (
    coap_init,
    coap_get_handler,
    coap_put_handler,
    coap_stop,
    coap_create_address,
    coap_free_address,
    coap_create_resource_attr,
    coap_free_resource_attr
  );

  /* Initialize a new device service */
  devsdk_service_t *service = devsdk_service_new
    ("device-coap", VERSION, impl, coapImpls, &argc, argv, &e);
  ERR_CHECK (e);
  impl->service = service;

  int n = 1;
  while (n < argc)
  {
    if (strcmp (argv[n], "-h") == 0 || strcmp (argv[n], "--help") == 0)
    {
      printf ("Options:\n");
      printf ("  -h, --help\t\t\tShow this text\n");
      devsdk_usage ();
      return 0;
    }
    else
    {
      printf ("%s: Unrecognized option %s\n", argv[0], argv[n]);
      return 0;
    }
  }

  /* Create default Driver config and start the device service */
  iot_data_t *driver_map = iot_data_alloc_map (IOT_DATA_STRING);
  iot_data_string_map_add (driver_map, COAP_BIND_ADDR_KEY, iot_data_alloc_string ("0.0.0.0", IOT_DATA_REF));
  iot_data_string_map_add (driver_map, SECURITY_MODE_KEY, iot_data_alloc_string ("NoSec", IOT_DATA_REF));
  iot_data_string_map_add (driver_map, PSK_KEY_KEY, iot_data_alloc_string ("", IOT_DATA_REF));

  devsdk_service_start (service, driver_map, &e);
  ERR_CHECK (e);

  /* Run CoAP server */
  run_server(impl);

  devsdk_service_stop (service, true, &e);
  ERR_CHECK (e);

  devsdk_service_free (service);
  iot_data_free (driver_map);
  iot_data_free (impl->coap_bind_addr);
  iot_data_free (impl->psk_key);
  free (impl);
  puts ("Exiting gracefully");
  return 0;
}
