using System;
using System.IO;
using System.Net;
using System.Net.WebSockets;
using System.Text;
using System.Text.Json;
using System.Threading;
using System.Threading.Tasks;

using Nefarius.ViGEm.Client;
using Nefarius.ViGEm.Client.Targets;
using Nefarius.ViGEm.Client.Targets.Xbox360;

class Program
{
    // Expects JSON from browser:
    // {
    //   "buttons": { "a":true, "b":false, "x":false, "y":false, "lb":false, "rb":false, "back":false, "start":false, "ls":false, "rs":false, "up":false, "down":false, "left":false, "right":false },
    //   "axes": { "lx":0, "ly":0, "rx":0, "ry":0, "lt":0, "rt":0 }
    // }
    //
    // axes:
    //  lx,ly,rx,ry are -1..+1 floats
    //  lt,rt are 0..1 floats

    static async Task Main()
    {
        // Create virtual controller via the public factory method
        var client = new ViGEmClient();
        IXbox360Controller pad = client.CreateXbox360Controller();
        pad.Connect();

        Console.WriteLine("ViGEm virtual Xbox 360 controller connected.");

        // WebSocket server
        var http = new HttpListener();
        http.Prefixes.Add("http://127.0.0.1:8765/ws/");
        http.Start();

        Console.WriteLine("WebSocket server listening on ws://127.0.0.1:8765/ws/");
        Console.WriteLine("Waiting for browser connection...");

        while (true)
        {
            var ctx = await http.GetContextAsync();

            if (!ctx.Request.IsWebSocketRequest)
            {
                ctx.Response.StatusCode = 400;
                ctx.Response.Close();
                continue;
            }

            var wsCtx = await ctx.AcceptWebSocketAsync(subProtocol: null);
            Console.WriteLine("Browser connected.");

            try
            {
                await HandleClient(wsCtx.WebSocket, pad);
            }
            catch (Exception ex)
            {
                Console.WriteLine("Client error: " + ex);
            }

            Console.WriteLine("Browser disconnected.");
        }
    }

    static async Task HandleClient(WebSocket ws, IXbox360Controller pad)
    {
        var buffer = new byte[8192];

        while (ws.State == WebSocketState.Open)
        {
            var ms = new MemoryStream();
            WebSocketReceiveResult result;

            do
            {
                result = await ws.ReceiveAsync(new ArraySegment<byte>(buffer), CancellationToken.None);

                if (result.MessageType == WebSocketMessageType.Close)
                {
                    await ws.CloseAsync(WebSocketCloseStatus.NormalClosure, "bye", CancellationToken.None);
                    return;
                }

                ms.Write(buffer, 0, result.Count);
            }
            while (!result.EndOfMessage);

            var json = Encoding.UTF8.GetString(ms.ToArray());
            ApplyStateFromJson(pad, json);
        }
    }

    static void ApplyStateFromJson(IXbox360Controller pad, string json)
    {
        using var doc = JsonDocument.Parse(json);
        var root = doc.RootElement;

        if (root.TryGetProperty("buttons", out var buttons))
        {
            SetBtn(pad, buttons, "a", Xbox360Button.A);
            SetBtn(pad, buttons, "b", Xbox360Button.B);
            SetBtn(pad, buttons, "x", Xbox360Button.X);
            SetBtn(pad, buttons, "y", Xbox360Button.Y);
            SetBtn(pad, buttons, "lb", Xbox360Button.LeftShoulder);
            SetBtn(pad, buttons, "rb", Xbox360Button.RightShoulder);
            SetBtn(pad, buttons, "back", Xbox360Button.Back);
            SetBtn(pad, buttons, "start", Xbox360Button.Start);
            SetBtn(pad, buttons, "ls", Xbox360Button.LeftThumb);
            SetBtn(pad, buttons, "rs", Xbox360Button.RightThumb);
            SetBtn(pad, buttons, "up", Xbox360Button.Up);
            SetBtn(pad, buttons, "down", Xbox360Button.Down);
            SetBtn(pad, buttons, "left", Xbox360Button.Left);
            SetBtn(pad, buttons, "right", Xbox360Button.Right);
        }

        if (root.TryGetProperty("axes", out var axes))
        {
            float lx = GetF(axes, "lx");
            float ly = GetF(axes, "ly");
            float rx = GetF(axes, "rx");
            float ry = GetF(axes, "ry");
            float lt = GetF(axes, "lt");
            float rt = GetF(axes, "rt");

            pad.SetAxisValue(Xbox360Axis.LeftThumbX, FloatToShort(lx));
            pad.SetAxisValue(Xbox360Axis.LeftThumbY, FloatToShort(ly));
            pad.SetAxisValue(Xbox360Axis.RightThumbX, FloatToShort(rx));
            pad.SetAxisValue(Xbox360Axis.RightThumbY, FloatToShort(ry));

            pad.SetSliderValue(Xbox360Slider.LeftTrigger, FloatToByte01(lt));
            pad.SetSliderValue(Xbox360Slider.RightTrigger, FloatToByte01(rt));
        }

        pad.SubmitReport();
    }

    static void SetBtn(IXbox360Controller pad, JsonElement buttons, string key, Xbox360Button btn)
    {
        if (!buttons.TryGetProperty(key, out var v)) return;
        bool pressed = v.ValueKind == JsonValueKind.True;
        pad.SetButtonState(btn, pressed);
    }

    static float GetF(JsonElement obj, string key)
    {
        if (!obj.TryGetProperty(key, out var v)) return 0f;
        if (v.ValueKind == JsonValueKind.Number && v.TryGetSingle(out var f)) return f;
        return 0f;
    }

    static short FloatToShort(float v)
    {
        v = Math.Clamp(v, -1f, 1f);
        int i = (int)MathF.Round(v * 32767f);
        if (i < -32768) i = -32768;
        if (i > 32767) i = 32767;
        return (short)i;
    }

    static byte FloatToByte01(float v)
    {
        v = Math.Clamp(v, 0f, 1f);
        int i = (int)MathF.Round(v * 255f);
        if (i < 0) i = 0;
        if (i > 255) i = 255;
        return (byte)i;
    }
}