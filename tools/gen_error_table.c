/* Kullanım kılavuzu "Hata kodları" bölümünü firmware tablosundan üretir.
 * Kullanım: gen_error_table > hata_kodlari.md */
#include <stdio.h>
#include "error_codes.h"

int main(void)
{
    uint32_t n;
    const fe_error_t *t = fe_error_table(&n);
    printf("| Kod | Ekran mesajı | Tür | Olası neden | Yapılacak |\n");
    printf("| --- | --- | --- | --- | --- |\n");
    for (uint32_t i = 0; i < n; i++) {
        printf("| %c%03u | %s | %s | %s | %s |\n", t[i].prefix, t[i].code, t[i].title,
               fe_severity_text(t[i].severity), t[i].cause, t[i].action);
    }
    return 0;
}
