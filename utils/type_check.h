typedef int (*fn_ptr) (const char*);

fn_ptr get_validator (const char *type);

int validate_ipv4 (const char *);
int validate_ipv4net (const char *);
int validate_ipv4net_addr (const char *);
int validate_ipv4range (const char *);
int validate_ipv4_negate (const char *);
int validate_ipv4net_negate (const char *);
int validate_ipv4range_negate (const char *);
int validate_iptables4_addr (const char *);
int validate_protocol (const char *);
int validate_protocol_negate (const char *);
int validate_macaddr (const char *);
int validate_sys_macaddr (const char *);
int validate_macaddr_negate (const char *);
int validate_ipv6 (const char *);
int validate_ipv6net (const char *);
int validate_ipv6_negate (const char *);
int validate_ipv6net_negate (const char *);
int validate_hex16 (const char *);
int validate_hex32 (const char *);
int validate_ipv6_addr_param (const char *);
int validate_restrictive_filename (const char *);
int validate_no_bash_special (const char *);
int validate_u32 (const char *);
int validate_bool (const char *);
int validate_port (const char *);
int validate_portrange (const char *);
int validate_port_negate (const char *);
int validate_portrange_negate (const char *);

int validateType (const char *, const char *, int);

