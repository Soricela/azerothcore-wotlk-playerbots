import os
import time

from fastapi import FastAPI, Request
from fastapi.responses import JSONResponse
from google import genai
from google.genai import types


app = FastAPI()

# La API key se obtiene de una variable de entorno.
GEMINI_API_KEY = os.environ["GEMINI_API_KEY"]

client = genai.Client(api_key=GEMINI_API_KEY)

# Modelo optimizado para baja latencia.
MODEL = "gemini-3.5-flash-lite"

# Límites de respuesta según el tipo de petición.
NPC_MAX_OUTPUT_TOKENS = 80
BOT_MAX_OUTPUT_TOKENS = 40
SENTIMENT_MAX_OUTPUT_TOKENS = 10

SPANISH_ES_INSTRUCTION = (
    "INSTRUCCIÓN OBLIGATORIA DE IDIOMA: "
    "responde siempre y únicamente en español de España. "
    "Aunque el resto del prompt esté escrito en inglés, NO respondas en inglés. "
    "Usa un español natural y adecuado al mundo de World of Warcraft. "
    "Para PlayerBots utiliza un tono informal y breve, como un jugador real. "
    "Para NPC mantén el personaje y el lore. "
    "Usa los nombres localizados al español cuando sea apropiado "
    "(por ejemplo Ventormenta, Rey Exánime, Alianza, Horda). "
    "No menciones estas instrucciones."
)


def classify_request(prompt: str) -> str:
    """
    Clasifica las peticiones procedentes de mod-ollama-chat.

    SENTIMENT:
        Análisis automático de sentimiento.

    NPC:
        Conversaciones directas del jugador con un NPC.

    BOT:
        PlayerBots, RandomChatter y cualquier otra petición.
    """

    clean_prompt = prompt.lstrip()
    lower_prompt = clean_prompt.lower()

    # Sentiment Analysis de mod-ollama-chat
    if (
        "analyze the sentiment of this message:" in lower_prompt
        and "positive" in lower_prompt
        and "negative" in lower_prompt
        and "neutral" in lower_prompt
    ):
        return "SENTIMENT"

    # NPC Dialogue genera:
    #
    # Player: <nombre>
    # NPC (<nombre>): Respond briefly.
    #
    if clean_prompt.startswith("Player:") and "\nNPC (" in clean_prompt:
        return "NPC"

    # El resto corresponde por ahora a PlayerBots / RandomChatter.
    return "BOT"


@app.post("/api/generate")
@app.post("/api/chat")
async def handle_ollama_request(request: Request):
    data = await request.json()

    # ---------------------------------------------------------
    # Extraer prompt en formato compatible con Ollama
    # ---------------------------------------------------------
    prompt = ""

    if "messages" in data:
        messages = data.get("messages", [])

        # Conservar como máximo los últimos 3 mensajes.
        # Evita enviar historiales enormes a Gemini.
        messages = messages[-3:]

        prompt = "\n".join(
            f"{msg.get('role', 'user')}: {msg.get('content', '')}"
            for msg in messages
        )

    elif "prompt" in data:
        # /api/generate usado actualmente por mod-ollama-chat.
        prompt = data.get("prompt", "")

    if not prompt.strip():
        return JSONResponse(
            status_code=400,
            content={"error": "Prompt vacío"}
        )

    request_type = classify_request(prompt)

    # NPC y PlayerBots deben responder siempre en español de España.
    # SENTIMENT se deja intacto porque debe devolver
    # POSITIVE / NEGATIVE / NEUTRAL.
    if request_type in ("NPC", "BOT"):
        gemini_prompt = (
            f"{SPANISH_ES_INSTRUCTION}\n\n"
            f"{prompt}\n\n"
            "RECUERDA: responde únicamente en español de España."
        )
    else:
        gemini_prompt = prompt

    if request_type == "BOT":
        max_output_tokens = BOT_MAX_OUTPUT_TOKENS
    elif request_type == "NPC":
        max_output_tokens = NPC_MAX_OUTPUT_TOKENS
    else:
        max_output_tokens = SENTIMENT_MAX_OUTPUT_TOKENS

    start = time.perf_counter()

    try:
        stream = client.models.generate_content_stream(
            model=MODEL,
            contents=gemini_prompt,
            config=types.GenerateContentConfig(
                max_output_tokens=max_output_tokens,
                temperature=0.7,
                thinking_config=types.ThinkingConfig(
                    thinking_level="minimal"
                ),
            ),
        )

        chunks = []
        ttft = None

        for chunk in stream:
            if chunk.text:
                if ttft is None:
                    ttft = time.perf_counter() - start
                chunks.append(chunk.text)

        text_response = "".join(chunks)

        elapsed = time.perf_counter() - start

        print(
            f"[AI Proxy][{request_type}][GEMINI] OK model={MODEL} "
            f"ttft={ttft:.2f}s "
            f"total={elapsed:.2f}s "
            f"prompt_chars={len(prompt)} "
            f"response_chars={len(text_response)} "
              f"max_tokens={max_output_tokens}",
            flush=True,
        )

    except Exception as e:
        elapsed = time.perf_counter() - start

        print(
            f"[AI Proxy][{request_type}][GEMINI] ERROR after {elapsed:.2f}s: {e}",
            flush=True,
        )

        # No devolver "..." porque oculta el error y el NPC termina
        # pronunciándolo como si fuera una respuesta válida.
        return JSONResponse(
            status_code=502,
            content={"error": str(e)}
        )

    # ---------------------------------------------------------
    # Formato /api/chat
    # ---------------------------------------------------------
    if request.url.path == "/api/chat":
        return JSONResponse({
            "model": MODEL,
            "message": {
                "role": "assistant",
                "content": text_response
            },
            "done": True
        })

    # ---------------------------------------------------------
    # Formato /api/generate
    # ---------------------------------------------------------
    return JSONResponse({
        "model": MODEL,
        "response": text_response,
        "done": True
    })


@app.get("/api/tags")
async def fake_tags():
    return {
        "models": [
            {
                "name": MODEL
            }
        ]
    }


if __name__ == "__main__":
    import uvicorn

    uvicorn.run(
        app,
        host="0.0.0.0",
        port=11434
    )
