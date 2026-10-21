# Instalador híbrido de CardGPT

El instalador web detecta el estado real del Cardputer ADV antes de habilitar la escritura:

1. valida ESP32-S3, flash de 8 MB, stub USB y registros de seguridad;
2. lee y verifica el MD5 de la tabla de particiones;
3. si reconoce M5Launcher, valida su cabecera y busca CardGPT o un rango libre;
4. si no reconoce M5Launcher, ofrece una instalación independiente con un aviso distinto.

## Modo M5Launcher

- Una partición `CardGPT` o `cardgp` existente se actualiza en su dirección real.
- En una instalación nueva se crea una OTA alineada a 64 KiB, con el siguiente subtipo libre y el nombre visible `CardGPT`.
- No se eliminan, mueven, redimensionan ni renombran particiones existentes.
- Si no hay rango contiguo suficiente, se cancela y el usuario debe liberar una app desde M5Launcher.
- NVS no se lee ni se escribe. M5Launcher, Wi-Fi, sesión, ajustes y demás apps se conservan.
- Primero se escribe y verifica CardGPT; solo después se escribe y verifica la tabla nueva.

## Modo independiente

Solo se ofrece cuando no se detecta un M5Launcher compatible. La interfaz explica que borrará la flash completa y exige casilla y confirmación. Usa `CardGPT-full.bin` desde `0x0` y verifica los 8 MiB.

Los layouts M5Launcher reconocidos pero antiguos, ambiguos, dañados o sin `otadata` se bloquean; nunca se reinterpretan automáticamente como autorización para borrar.
