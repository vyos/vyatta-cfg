#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include "misc.h"
#include "type_check.h"

fn_ptr 
get_validator (const char *type) 
{
  if (strcmp(type, "ipv4") == 0) 
    return &validate_ipv4;
  else if (strcmp(type, "ipv4net") == 0) 
    return &validate_ipv4net;
  else if (strcmp(type, "ipv4net_addr") == 0) 
    return &validate_ipv4net_addr;
  else if (strcmp(type, "ipv4range") == 0)
    return &validate_ipv4range;
  else if (strcmp(type, "ipv4_negate") == 0)
    return &validate_ipv4_negate;
  else if (strcmp(type, "ipv4net_negate") == 0)
    return &validate_ipv4net_negate;
  else if (strcmp(type, "ipv4range_negate") == 0)
    return &validate_ipv4range_negate;
  else if (strcmp(type, "iptables4_addr") == 0)
    return &validate_iptables4_addr;
  else if (strcmp(type, "protocol") == 0)
    return &validate_protocol;
  else if (strcmp(type, "protocol_negate") == 0)
    return &validate_protocol_negate;
  else if (strcmp(type, "macaddr") == 0)
    return &validate_macaddr;
  else if (strcmp(type, "sys_macaddr") == 0)
    return &validate_sys_macaddr;
  else if (strcmp(type, "macaddr_negate") == 0)
    return &validate_macaddr_negate;
  else if (strcmp(type, "ipv6") == 0)
    return &validate_ipv6;
  else if (strcmp(type, "ipv6_negate") == 0)
    return &validate_ipv6_negate;
  else if (strcmp(type, "ipv6net") == 0)
    return &validate_ipv6net;
  else if (strcmp(type, "ipv6net_negate") == 0)
    return &validate_ipv6net_negate;
  else if (strcmp(type, "hex16") == 0)
    return &validate_hex16;
  else if (strcmp(type, "hex32") == 0)
    return &validate_hex32;
  else if (strcmp(type, "ipv6_addr_param") == 0)
    return &validate_ipv6_addr_param;
  else if (strcmp(type, "restrictive_filename") == 0)
    return &validate_restrictive_filename;
  else if (strcmp(type, "no_bash_special") == 0)
    return &validate_no_bash_special;
  else if (strcmp(type, "u32") == 0)
    return &validate_u32;
  else if (strcmp(type, "bool") == 0)
    return &validate_bool;
  else if (strcmp(type, "port") == 0)
    return &validate_port;
  else if (strcmp(type, "portrange") == 0)
    return &validate_portrange;
  else if (strcmp(type, "port_negate") == 0)
    return &validate_port_negate;
  else if (strcmp(type, "portrange_negate") == 0)
    return &validate_portrange_negate;
  else
    return NULL;
  return NULL;
}

int
validate_ipv4 (const char *str)
{
  if (!str)
    return 0;
  unsigned int a[4];
  if (strlen(str) == 0){
    return 0;
  }
  if (!re_match(str, "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$"))
    return 0;
  if (sscanf(str, "%u.%u.%u.%u", &a[0], &a[1], &a[2], &a[3])
      != 4)
    return 0;
  int i;
  for (i = 0; i < 4; i++) {
    if (a[i] > 255)
      return 0;              
  }  
  return 1;
}

int
validate_ipv4net (const char *str)
{
  if (!str)
    return 0;
  unsigned int a[4], plen;
  uint32_t addr;
  if (!re_match(str, "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+/[0-9]+$"))
    return 0;
  if (sscanf(str, "%u.%u.%u.%u/%u", &a[0], &a[1], &a[2], &a[3], &plen) 
      != 5) 
     return 0;
  addr = 0;
  int i;
  for (i = 0; i < 4; i++) {
    if (a[i] > 255)
      return 0;              
    addr <<= 8;
    addr |= a[i];
  }
  if ((plen == 0 && addr != 0) || plen > 32)
    return 0;
  return 1;
}

int
validate_ipv4net_addr (const char *str)
{
  if (!str)
    return 0;
  unsigned int a[4], plen;
  uint32_t addr;
  if (!re_match(str, "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+/[0-9]+$"))
    return 0;
  if (sscanf(str, "%u.%u.%u.%u/%u", &a[0], &a[1], &a[2], &a[3], &plen) 
      != 5) 
     return 0;
  addr = 0;
  int i;
  for (i = 0; i < 4; i++) {
    if (a[i] > 255)
      return 0;              
    addr <<= 8;
    addr |= a[i];
  }
  if ((plen == 0 && addr != 0) || plen > 32)
    return 0;
  if (plen < 31) {
    uint32_t net_mask = ~0 << (32 - plen);
    uint32_t broadcast = (addr & net_mask) | (~0 &~ net_mask);
    if ((addr & net_mask) != addr) 
      return 0;
    if (broadcast != 0 && addr == broadcast)
      return 0;
  }
  return 1;
}

int
validate_ipv4range (const char *str)
{
  if (!str)
    return 0;
  if (!re_match(str, 
  "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+-[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$"))
    return 0;

  uint32_t addr1 = 0;
  uint32_t addr2 = 0;
  unsigned int a1[4], a2[4];
  if (sscanf(str, "%u.%u.%u.%u-%u.%u.%u.%u", 
                   &a1[0], &a1[1], &a1[2], &a1[3],
                   &a2[0], &a2[1], &a2[2], &a2[3]) != 8)
    return 0;
  int i;
  for (i = 0; i < 4; i++) {
    if (a1[i] > 255)
      return 0;              
    addr1 <<= 8;
    addr1 |= a1[i];

    if (a2[i] >255)
      return 0;
    addr2 <<=8;
    addr2 |= a2[i];
  }
  if (addr1 >= addr2)
    return 0;
  return 1;
}

int
validate_ipv4_negate (const char *str)
{
  if (!str)
    return 0;
  char ipv4[15];
  memset(ipv4, '\0', 15);
  if (sscanf(str, "!%15s", ipv4) != 1)
    return validate_ipv4(str);
  return validate_ipv4(ipv4); 
}

int
validate_ipv4net_negate (const char *str)
{
  if (!str)
    return 0;
  char ipv4net[18];
  memset(ipv4net, '\0', 18);
  if (sscanf(str, "!%18s", ipv4net) != 1)
    return validate_ipv4net(str);
  return validate_ipv4net(ipv4net);
}

int
validate_ipv4range_negate (const char *str)
{
  if (!str)
    return 0;
  char ipv4[31];
  memset(ipv4, '\0', 31);
  if (sscanf(str, "!%31s", ipv4) != 1)
    return validate_ipv4range(str);
  return validate_ipv4range(ipv4); 
}

int
validate_iptables4_addr (const char *str)
{
  if (!str)
    return 0;
  if (!validate_ipv4_negate(str) &&
      !validate_ipv4net_negate(str) &&
      !validate_ipv4range_negate(str))
    return 0;
  return 1;
}

int
validate_protocol (const char *str)
{ 
  if (!str)
    return 0;
  if (strcmp(str, "all") == 0)
    return 1;
  if (strcmp(str, "ip") == 0 || strcmp(str, "0") == 0)
    return 1;
  if (re_match(str, "^[0-9]+$")) {
    int val = atoi(str);
    if (val >= 1 && val <= 255)
      return 1;
  }
  struct protoent *p;
  p = getprotobyname(str);
  if (!p)
    return 0;
  if (p->p_proto) {
    return 1;
  }
  return 0;
}

int
validate_protocol_negate (const char *str)
{
  if (!str)
    return 0;
  char proto[100];
  memset(proto, '\0', 100);
  if (sscanf(str, "!%100s", proto) != 1)
    return validate_protocol(str);
  return validate_protocol(proto);
}

int
validate_sys_macaddr (const char *str)
{
    if (!str)
      return 0;
    if (!validate_macaddr(str))
      return 0;

    int a[6];
    int sum = 0;
      
    if (sscanf(str, "%x:%x:%x:%x:%x:%x", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]) 
	!= 6) {
      printf("Error: wrong number of octets\n"); 
      return 0;
    }
    
    if (a[0] & 1){
      printf("Error: %x:%x:%x:%x:%x:%x is a multicast address\n",a[0],a[1],a[2],a[3],a[4],a[5]);
      return 0;
    }

    if ((a[0] == 0) && (a[1] == 0) && (a[2] == 94) &&(a[3] == 0) && (a[4] == 1)) {
      printf("Error: %x:%x:%x:%x:%x:%x is a vrrp mac address\n",a[0],a[1],a[2],a[3],a[4],a[5]);
      return 0;
    }

    int i;
    for (i=0; i<6; ++i){
      sum += a[i];
    }

    if (sum == 0){
      printf("Error: zero is not a valid address\n");
      return 0;
    }
    return 1;
}

int
validate_macaddr (const char *str)
{
  if (!str)
    return 0;
  return re_match(str, "^[0-9a-fA-F]{2}(:[0-9a-fA-F]{2}){5}$");
}

int
validate_macaddr_negate (const char *str)
{
  if (!str)
    return 0;
  char macaddr[17];
  memset(macaddr,'\0',17);
  if (sscanf(str, "!%17s", macaddr) != 1)
    return validate_macaddr(str);
  return validate_macaddr(macaddr);
}

int
validate_ipv6 (const char *str)
{
  if (!str)
    return 0;
  struct in6_addr addr;
  if (inet_pton(AF_INET6, str, &addr) <= 0)
    return 0;
  return 1;
}

int
validate_ipv6net (const char *str)
{
  if (!str)
    return 0;
  unsigned int prefix_len;
  struct in6_addr addr;
  char *slash, *endp;

  slash = strchr(str, '/');
  if (!slash)
    return 0;
  *slash++ = 0;
  prefix_len = strtoul(slash, &endp, 10);
  if (*slash == '\0' || *endp != '\0')
    return 0;
  else if (prefix_len > 128)
    return 0;
  else if (inet_pton(AF_INET6, str, &addr) <= 0)
    return 0;
  else
    return 1;
}

int 
validate_ipv6_negate (const char *str)
{
  if (!str)
    return 0;
  char ipv6[39];
  memset(ipv6, '\0', 39);
  if (sscanf(str, "!%39s", ipv6) != 1)
    return validate_ipv6(str);
  return validate_ipv6(ipv6);
}

int
validate_ipv6net_negate (const char *str)
{
  if (!str)
    return 0;
  char ipv6net[43];
  memset(ipv6net, '\0', 43);
  if (sscanf(str, "!%43s", ipv6net) != 1)
    return validate_ipv6net(str);
  return validate_ipv6net(ipv6net); 
}

int
validate_hex16 (const char *str)
{
  if (!str)
    return 0;
  return re_match(str, "^[0-9a-fA-F]{4}$");
}

int
validate_hex32 (const char *str)
{
  if (!str)
    return 0;
  return re_match(str, "^[0-9a-fA-F]{8}$");
}

int
validate_ipv6_addr_param (const char *str)
{
  if (!str)
    return 0;
  char value[87];
  char ipv6_1[43];
  char ipv6_2[43];
  memset(ipv6_1, '\0', 43);
  memset(ipv6_2, '\0', 43);
  if (sscanf(str, "!%87s", value) == 1)
    str = value;
  if (re_match(str, "^[^-]+-[^-]+$")){
    char *dash = strrchr(str, '-');
    strncpy(ipv6_1, str, dash - str);
    strncpy(ipv6_2, dash+1, strchr(str,'\0') - dash+1);
    if (validate_ipv6(ipv6_1))
      return validate_ipv6(ipv6_2);
    else
      return 0;
  }
  if (strchr(str, '/') != NULL)
    return validate_ipv6net(str);
  else
    return validate_ipv6(str);
}

int
validate_restrictive_filename (const char *str)
{
  if (!str)
    return 0;
  return re_match(str, "^[-_.a-zA-Z0-9]+$");
}

int
validate_no_bash_special (const char *str)
{
  if (!str)
    return 0;
  return (!re_match(str,"[;&\"'`!$><|]"));
}

int
validate_u32 (const char *str)
{
  if (!str)
    return 0;
  if (!re_match(str, "^[0-9]+$"))
    return 0;
  unsigned long int val = strtoul(str, NULL, 0);
  unsigned long int max = 4294967296;
  if (val > max)
    return 0;
  return 1;
}

int
validate_bool (const char *str)
{
  if (!str)
    return 0;
  if (strcmp(str, "true") == 0)
    return 1;
  else if (strcmp(str, "false") == 0)
    return 1;
  return 0;
}

int 
validate_port (const char * str) 
{
  if (!str)
    return 0;
  int port;
  struct servent *s;
  if (re_match(str, "^[0-9]+$")) {
    port = atoi(str);
    if ( port < 1 || port > 65535 ) 
      return 0;
    else 
      return 1;
  } else {
    s = getservbyname(str, NULL);
    if (!s) 
      return 0;
    if (s->s_port)
      return 1;
    else
      return 0;
  }
}

int 
validate_portrange (const char * str)
{
  if (!str)
    return 0;
  int start, stop;
  start = stop = 0;
  char start_str[6], stop_str[6];
  memset(start_str, '\0', 6);
  memset(stop_str, '\0', 6);
  if (!re_match(str, "^[0-9]+-[0-9]+$"))
    return 0;
  if (sscanf(str, "%d-%d", &start, &stop) != 2)
    return 0;
  sprintf(start_str, "%d", start);
  sprintf(stop_str, "%d", stop);
  if (!validate_port(start_str))
    return 0;
  if (!validate_port(stop_str))
    return 0;
  if (stop <= start) {
    return 0;
  }
  return 1;  
}

int 
validate_port_negate (const char *str)
{
  if (!str)
    return 0;
  char port[5];
  memset(port, '\0', 5);
  if (sscanf(str, "!%5s", port) != 1)
    return validate_port(str);
  return validate_port(port);
}

int 
validate_portrange_negate (const char *str)
{
  if (!str)
    return 0;
  char port[11];
  memset(port, '\0', 11);
  if (sscanf(str, "!%11s", port) != 1)
    return validate_portrange(str);
  return validate_portrange(port);
}

int
validateType (const char *type, const char *str, int quiet)
{
  if (!str)
    return 0;
  int (*validator)(const char *) = NULL;
  validator = get_validator(type);
  if (validator == NULL) {
    if (!quiet)
      printf("type: \"%s\" is not defined\n", type);
    return 0;
  }
  if (!(*validator)(str)) {
    if (!quiet)
      printf("\"%s\" is not a valid value of type \"%s\"\n", str, type);
    return 0;
  }
  return 1;
}
