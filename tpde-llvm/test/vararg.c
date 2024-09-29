#include <stdarg.h>
#include <stdio.h>

// https://jameshfisher.com/2016/11/23/c-varargs/
void my_printf(char* format, unsigned normalInt, double normalFloat, ...) {
   va_list argp;
   va_start(argp, normalFloat);
   printf("%d %f: ", normalInt, normalFloat);
   while (*format != '\0') {
      if (*format == '%') {
         format++;
         if (*format == '%') {
            putchar('%');
         } else if (*format == 'c') {
            char char_to_print = va_arg(argp, int);
            putchar(char_to_print);
         } else if (*format == 'f') {
            double f = va_arg(argp, double);
            printf("%f", f);
         } else {
            fputs("Not implemented", stdout);
         }
      } else {
         putchar(*format);
      }
      format++;
   }
   va_end(argp);
}

int main() {
   my_printf("This is a test %c%c%c%c%c%c%c%c%c%c: %f %f %f %f %f %f %f %f %f", 42, 1337.f, 's', 't', 'r', 'i', 'n', 'g', ' ', 'a', 'r', 'g', 1.f, 2.f, 3.f, 4.f, 5.f, 6.f, 7.f, 8.f, 9.f);
   return 0;
}
