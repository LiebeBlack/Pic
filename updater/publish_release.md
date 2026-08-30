# Publicar el exe en GitHub

## 1) Requisitos

- Tener el repositorio en GitHub
- Haber lanzado el workflow de release
- El archivo final debe llamarse exactamente: `artpicst-installer.exe`

## 2) Regla importante

La app de actualización solo compara versiones con la ultima release de GitHub. Si la tag es igual o menor, no actualiza.

Ejemplos validos:
- `v1.0.0`
- `1.0.0`
- `v1.2.3-beta` -> se limpia a `1.2.3`

## 3) Flujo recomendado

1. Subir el proyecto a GitHub
2. Ejecutar el workflow de release
3. Confirmar que el artefacto `artpicst-installer.exe` aparece en la release
4. Dejar el updater apuntando a `GITHUB_OWNER` y `GITHUB_REPO`
5. La comparacion funciona con la `tag` del ultimo release

## 4) Regla de comparacion

El updater hace esto:

- convierte la tag a version numerica
- elimina prefijos `v`
- compara `latest > current`
- si es menor o igual, no instala nada

Esto evita false positives y actualizaciones repetidas.
