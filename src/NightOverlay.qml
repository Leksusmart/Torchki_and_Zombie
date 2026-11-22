import QtQuick 2.15

// Слой ночного затемнения с вырезанием круглых зон освещения вокруг горящих зданий
Canvas {
    id: root
    required property var world
    required property real camX
    required property real camY
    required property real camScale

    enabled: false
    visible: world.nightDarkness > 0.001

    property real darkness: world.nightDarkness
    property var lights: world.burningLights

    onDarknessChanged: requestPaint()
    onLightsChanged: requestPaint()
    onCamXChanged: requestPaint()
    onCamYChanged: requestPaint()
    onCamScaleChanged: requestPaint()

    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        if (darkness <= 0.001) return

        ctx.globalCompositeOperation = "source-over"
        ctx.fillStyle = Qt.rgba(0.024, 0.043, 0.098, darkness)
        ctx.fillRect(0, 0, width, height)

        if (lights && lights.length > 0) {
            ctx.globalCompositeOperation = "destination-out"
            for (var i = 0; i < lights.length; ++i) {
                var sx = camX + lights[i].cx * camScale
                var sy = camY + lights[i].cy * camScale
                var sr = lights[i].radius * camScale

                var grad = ctx.createRadialGradient(sx, sy, sr * 0.25, sx, sy, sr)
                grad.addColorStop(0.0, "rgba(0, 0, 0, 1.0)")
                grad.addColorStop(0.7, "rgba(0, 0, 0, 0.85)")
                grad.addColorStop(1.0, "rgba(0, 0, 0, 0.0)")

                ctx.fillStyle = grad
                ctx.beginPath()
                ctx.arc(sx, sy, sr, 0, Math.PI * 2)
                ctx.fill()
            }
        }
    }
}
