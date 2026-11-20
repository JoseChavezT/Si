# Tryled

Emulador de PSP personal para Android (Samsung Galaxy S24 Ultra), basado en el
código abierto de [PPSSPP](https://github.com/hrydgard/ppsspp) **v1.20.4** con
una interfaz propia. Se compila desde el código fuente, sin binarios de terceros.

El README original de PPSSPP está en [`README-PPSSPP.md`](README-PPSSPP.md).

## Cambios respecto a PPSSPP

- Nombre de la app: **Tryled** (`android/res/values/strings.xml`).
- `applicationId`: `com.tryled.emu` (variante `normal`), así se puede instalar
  junto a un PPSSPP oficial sin conflicto. El paquete Java interno sigue siendo
  `org.ppsspp.ppsspp` porque el código nativo (JNI) depende de ese nombre.

## Compilar

```bash
git clone --recurse-submodules <url-de-este-repo>
# o, si ya está clonado:
git submodule update --init --recursive
```

Luego abre la carpeta en Android Studio (con el NDK y CMake instalados) y
compila la variante `normalOptimizedRelease` para el teléfono (arm64-v8a).

Nota: el submódulo `ffmpeg` de PPSSPP trae librerías precompiladas. Si no
quieres ningún binario de terceros, se pueden recompilar desde el código de
FFmpeg con los scripts de ese submódulo (`android_arm64_v8a.sh`).

## Licencia

PPSSPP se distribuye bajo GPL v2 o posterior; Tryled hereda esa licencia.
