# GUI

El proyecto de interfaz gráfica diseñado con [EEZ Studio](https://www.envox.eu/studio/) para LVGL se encuentra en este directorio.
El código fuente en C generado automáticamente se exporta a [`../main/ui/`](../main/ui/) — no editar esos archivos manualmente.

## Flujo de Trabajo

1. Abre el archivo `.eez-project` de esta carpeta con **EEZ Studio**.
2. Diseña o modifica las pantallas y presiona **Generate Code** (`F7`) o haz clic en **Build**.
3. Los archivos generados se guardarán automáticamente en `../main/ui/`.
4. Desde la terminal o VS Code en la raíz del proyecto, ejecuta la compilación:
   ```bash
   idf.py build
   ```
