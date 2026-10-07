#pragma once

#include <spot.h>

namespace Spot::Editor {

// What moved the editor camera this frame.
struct EditorCameraInput {
    // How far the mouse moved, in pixels.
    Vector2 mouseDelta = {};
    // Wheel notches, positive away from the user.
    float wheel = 0.0f;
    // Right button held: turn the view and fly with the movement keys.
    bool look = false;
    // Turn around the pivot.
    bool orbit = false;
    // Slide the view sideways and up or down.
    bool pan = false;
    // Movement keys while looking: x right, y up, z forward, each from -1 to 1.
    Vector3 move = {};
    bool fast = false;
    float deltaTime = 0.0f;
    // The view's height in pixels, so panning follows the mouse.
    float viewHeight = 1.0f;
};

// The camera a scene view looks through: it turns around a pivot it looks at, which flying carries along.
class EditorCamera {
public:
    // Looks at the origin from above, at an angle.
    EditorCamera();

    Camera3D GetCamera() const;
    Vector3 GetPosition() const;
    Quaternion GetRotation() const;

    Vector3 GetPivot() const { return m_pivot; }
    float GetDistance() const { return m_distance; }
    float GetYaw() const { return m_yaw; }
    float GetPitch() const { return m_pitch; }
    // Flying speed in units per second.
    float GetSpeed() const { return m_speed; }
    void SetSpeed(float speed);
    // Places the camera exactly, as when restoring a saved view.
    void SetView(Vector3 pivot, float yaw, float pitch, float distance);

    void Update(const EditorCameraInput& input);
    // Moves to look at the sphere from the current direction, filling about the view with it.
    void Frame(Vector3 center, float radius);

private:
    Vector3 m_pivot;
    float m_yaw;
    float m_pitch;
    float m_distance;
    float m_speed = 5.0f;
    float m_fieldOfView = ToRadians(60.0f);
};

} // namespace Spot::Editor
