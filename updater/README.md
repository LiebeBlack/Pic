# ARTPICST Updater

Updater independiente para comprobar la ultima version en GitHub y descargar el instalador.

## Configuracion

Edita `config.py` y pon:

- `GITHUB_OWNER`: tu usuario o organizacion de GitHub
- `GITHUB_REPO`: nombre del repositorio
- `CURRENT_VERSION`: version actual de la aplicacion
- `INSTALLER_ASSET_NAME`: nombre exacto del archivo .exe publicado en GitHub Releases

## Ejecutar

```bat
python updater.py
```

## Comportamiento

- consulta la ultima release de GitHub
- compara version con la actual
- pregunta si quieres actualizar
- descarga el installer del release
- desinstala la version previa si existe
- ejecuta la nueva instalacion silenciosa
