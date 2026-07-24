# Captura la Bandera

## Reglamento básico

## Objetivo

Cada jugador compite de forma individual. El objetivo es:

1. Entrar al círculo central.
2. Tomar la bandera.
3. Salir completamente del círculo mientras continúa llevando la bandera.

El primer jugador que lo consiga gana la partida.

---

## Inicio de la partida

- Existe una única bandera ubicada exactamente en el centro del mapa.
- Todos los jugadores aparecen en posiciones aleatorias fuera del círculo.
- Ningún jugador inicia con la bandera.
- La partida comienza cuando el servidor envía la señal de inicio.

---

## Movimiento

Los jugadores pueden desplazarse libremente por el mapa utilizando los controles que se configuren en el teclado.

Algunos conceptos que pueden enviar o recibir de los jugadores (suelen ser más):

- Nombre
- Posición
- Dirección
- Estado del jugador
- Colisión / Interacción / Acción

---

# Captura de la bandera

Cuando un jugador se encuentra suficientemente cerca de la bandera, puede presionar la tecla de interacción (por ejemplo **E**, aunque esto puede ser totalmente arbitrario dependiendo del proyecto).

Si la bandera está libre:

- Pasa inmediatamente a pertenecer a ese jugador.

A partir de ese momento:

- La bandera deja de estar en el suelo.
- La bandera acompaña al jugador.

---

## Robo de la bandera

Si un jugador posee la bandera, cualquier otro jugador puede robársela.

### Condiciones

- Debe estar suficientemente cerca del portador.
- Debe presionar la tecla de interacción.

Si ambas condiciones se cumplen:

- La bandera cambia inmediatamente de propietario.

### Reglas

- No existe tiempo de espera.
- No existe inmunidad.
- El robo es instantáneo.
- Si existen más jugadores cerca, solo se valida el primero; posteriormente los demás deberán volver a presionar la tecla para robar al nuevo portador.

---

# Condición de victoria

Un jugador gana cuando:

1. Tiene la bandera.
2. Cruza completamente el límite del círculo hacia el exterior.

No basta con tocar el borde.

Debe encontrarse totalmente fuera del área de juego central.

Cuando esto ocurre:

- El servidor anuncia al ganador.
- Termina la partida para todos los jugadores.

---

# Implementación del Proyecto

- Este proyecto se realizará de forma individual.
- El desarrollo del proyecto puede realizarse en cualquier lenguaje de programación.
- El proyecto puede utilizar cualquier librería que el lenguaje permita, así como sockets.
- La interfaz gráfica puede desarrollarse con la tecnología que el estudiante prefiera, siempre que cumpla con lo básico del juego Captura la Bandera.
- El proyecto debe comportarse como servidor o cliente de otro juego.
- Debe soportar **N** usuarios conectados (límite de **100**).
- Se puede utilizar cualquier inteligencia artificial para apoyar el aprendizaje del lenguaje de programación o alguna implementación específica.

---

## Limitaciones de implementación

- No pueden existir más de **4 proyectos** desarrollados en un mismo lenguaje de programación.
- No pueden existir más de **2 proyectos** que utilicen la misma librería de conexión o de generación de gráficas. La excepción es utilizar sockets básicos.
- Cuando el proyecto se configure como **servidor**, únicamente debe mostrar el juego de todos los jugadores. Solo cuando esté configurado como **cliente** conectado a un servidor será posible jugar desde esa máquina.

---

## Recomendaciones

- Utilizar **Broadcast** para la comunicación general, permitiendo descubrir servidores disponibles, iniciar nuevas partidas y realizar un **Countdown** para confirmar el inicio del juego.
- Todas las validaciones para determinar un ganador deben ejecutarse en el **servidor**. Los clientes únicamente reaccionan a los eventos o banderas enviados por el servidor para actualizar el estado del juego.
- Todos los jugadores deben visualizar el movimiento de todos los demás jugadores en todos los clientes conectados, no únicamente en el servidor.

---

# Entregable

- Juego funcional.
- Servidor con:
  - Soporte multijugador.
  - Visualización de jugadores.
  - Validación del juego.
- Cliente con:
  - Descubrimiento automático de servidores.
  - Capacidad para unirse a partidas que aún no hayan iniciado.
  - Validación de estados.
  - Visualización de todos los jugadores conectados.

## Entorno gráfico

Puede implementarse como:

- Web
- 3D
- 2D
- ASCII
- Etc.

---

## Soporte de conexiones

- Debe soportar múltiples conexiones.
- Debe ser capaz de soportar hasta **100 conexiones**.
- El límite real dependerá de la cantidad de proyectos entregados.
- Todos los proyectos deben poder conectarse entre sí. No existe la posibilidad de que únicamente algunos proyectos sean compatibles. Si únicamente una minoría logra comunicarse correctamente, esos proyectos recibirán una calificación de **cero**.
- En esta asignación no existen grupos; el grupo es toda la clase.

---

## Documentación de implementación

Debe incluir:

- Documentación desde la versión **1**.
- Registro desde el primer día de desarrollo hasta la entrega final.
- Historial completo de cambios de ideas dentro del documento, referenciado a **Git**.
- Explicación de las conexiones utilizadas para lograr la comunicación entre todos los proyectos.
- Uso de **Git** para mantener la cronología del progreso y el registro (log) de la documentación.