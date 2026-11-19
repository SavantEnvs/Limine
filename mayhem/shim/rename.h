/* Force-included into every Limine TU compiled for the host: Limine defines its own copies of
 * symbols that also exist in the host libc. Rename Limine's so both can be linked into one
 * process (the host libc provides mem and str functions; Limine's libc*.c are not linked). */
#define fopen   lm_fopen
#define fread   lm_fread
#define fclose  lm_fclose
#define getchar lm_getchar
#define print   lm_print
#define vprint  lm_vprint
#define panic   lm_panic
#define strdup  lm_strdup
#define inet_pton lm_inet_pton
#define abs     lm_abs
