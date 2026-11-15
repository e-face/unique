#include "modelprofilepresets.h"
//----------------------------------------------
#include <QObject>

//----------------------------------------------

static ModelProfilePreset makePreset(QString provider, QString name, WorkerTaskKind modality,
                                     QString baseUrl, QString model,
                                     QString balanceUrl = QString(),
                                     QString balanceJsonPath = QString(),
                                     QString purpose = QString())
{
    ModelProfilePreset p;
    p.provider = provider;
    p.name = name;
    p.modality = modality;
    p.baseUrl = baseUrl;
    p.model = model;
    p.balanceUrl = balanceUrl;
    p.balanceJsonPath = balanceJsonPath;
    p.purpose = purpose;
    return p;
}

//----------------------------------------------

QList<ModelProfilePreset> knownModelProfilePresets()
{
    QList<ModelProfilePreset> list;

    // --- LLM ---
    list << makePreset(QString::fromUtf8("DeepSeek"), QString::fromUtf8("DeepSeek Chat"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.deepseek.com"),
                       QString::fromUtf8("deepseek-chat"),
                       QString::fromUtf8("/user/balance"), QString(),
                       QString::fromUtf8("Text-only LLM: coding and agent tool use"));
    list << makePreset(QString::fromUtf8("DeepSeek"), QString::fromUtf8("DeepSeek Reasoner"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.deepseek.com"),
                       QString::fromUtf8("deepseek-reasoner"),
                       QString::fromUtf8("/user/balance"), QString(),
                       QString::fromUtf8("Deep reasoning / hard bugs (text-only)"));
    list << makePreset(QString::fromUtf8("DeepSeek"), QString::fromUtf8("DeepSeek Flash (V4.1)"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.deepseek.com"),
                       QString::fromUtf8("deepseek-flash"),
                       QString::fromUtf8("/user/balance"), QString(),
                       QString::fromUtf8(
                           "Multimodal LLM: text + image input, text output"));
    list << makePreset(QString::fromUtf8("DeepSeek"), QString::fromUtf8("DeepSeek V4 Flash"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.deepseek.com"),
                       QString::fromUtf8("deepseek-v4-flash"),
                       QString::fromUtf8("/user/balance"), QString(),
                       QString::fromUtf8(
                           "Alias of Flash: text + image input (routes to V4.1-Flash)"));

    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat"), QString(), QString(),
                       QString::fromUtf8(
                           "Chat model. Token field is a short-lived access token, about 30 minutes."));
    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat Pro"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat-Pro"), QString(), QString(),
                       QString::fromUtf8(
                           "Stronger GigaChat chat model. Token is a short-lived access token."));
    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat Max"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat-Max"), QString(), QString(),
                       QString::fromUtf8(
                           "Largest GigaChat chat model. Token is a short-lived access token."));
    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat 2"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat-2"), QString(), QString(),
                       QString::fromUtf8("GigaChat 2 chat model. Token is a short-lived access token."));
    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat 2 Pro"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat-2-Pro"), QString(), QString(),
                       QString::fromUtf8("GigaChat 2 Pro. Token is a short-lived access token."));
    list << makePreset(QString::fromUtf8("GigaChat"), QString::fromUtf8("GigaChat 2 Max"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.giga.chat/v1"),
                       QString::fromUtf8("GigaChat-2-Max"), QString(), QString(),
                       QString::fromUtf8("GigaChat 2 Max. Token is a short-lived access token."));

    list << makePreset(QString::fromUtf8("Alice AI"), QString::fromUtf8("Alice AI LLM"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://ai.api.cloud.yandex.net/v1"),
                       QString::fromUtf8("gpt://FOLDER_ID/aliceai-llm/latest"),
                       QString(), QString(),
                       QString::fromUtf8(
                           "Yandex Alice AI. Replace FOLDER_ID in the model id with the AI Studio folder id."));

    list << makePreset(QString::fromUtf8("Cotype"), QString::fromUtf8("Cotype Pro 2"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://HOST:8000/v1"),
                       QString::fromUtf8("cotype_pro_2"), QString(), QString(),
                       QString::fromUtf8(
                           "MTS Cotype Pro. Replace HOST with the stand address."));
    list << makePreset(QString::fromUtf8("Cotype"), QString::fromUtf8("Cotype"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://HOST:8000/v1"),
                       QString::fromUtf8("cotype"), QString(), QString(),
                       QString::fromUtf8(
                           "MTS Cotype. Replace HOST with the stand address."));

    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("GPT-4.1"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-4.1"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("GPT-4o"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-4o"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("GPT-4o mini"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-4o-mini"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("o3"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("o3"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("o4-mini"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("o4-mini"));

    list << makePreset(QString::fromUtf8("Groq"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://api.groq.com/openai/v1"),
                       QString::fromUtf8("llama-3.3-70b-versatile"));
    list << makePreset(QString::fromUtf8("Groq"), QString::fromUtf8("Llama 4 Scout"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://api.groq.com/openai/v1"),
                       QString::fromUtf8("meta-llama/llama-4-scout-17b-16e-instruct"));
    list << makePreset(QString::fromUtf8("Groq"), QString::fromUtf8("Qwen3 32B"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://api.groq.com/openai/v1"),
                       QString::fromUtf8("qwen/qwen3-32b"));

    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("Qwen Plus"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("qwen-plus"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));
    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("Qwen Max"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("qwen-max"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));
    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("Qwen3.7 Plus"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("qwen3.7-plus"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));

    list << makePreset(QString::fromUtf8("Google"), QString::fromUtf8("Gemini 2.5 Flash"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://generativelanguage.googleapis.com/v1beta/openai"),
                       QString::fromUtf8("gemini-2.5-flash"));
    list << makePreset(QString::fromUtf8("Google"), QString::fromUtf8("Gemini 2.5 Pro"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://generativelanguage.googleapis.com/v1beta/openai"),
                       QString::fromUtf8("gemini-2.5-pro"));

    list << makePreset(QString::fromUtf8("xAI"), QString::fromUtf8("Grok 3"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.x.ai/v1"),
                       QString::fromUtf8("grok-3"));
    list << makePreset(QString::fromUtf8("xAI"), QString::fromUtf8("Grok 3 mini"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.x.ai/v1"),
                       QString::fromUtf8("grok-3-mini"));

    list << makePreset(QString::fromUtf8("Mistral"), QString::fromUtf8("Mistral Large"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.mistral.ai/v1"),
                       QString::fromUtf8("mistral-large-latest"));
    list << makePreset(QString::fromUtf8("Mistral"), QString::fromUtf8("Codestral"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.mistral.ai/v1"),
                       QString::fromUtf8("codestral-latest"));

    list << makePreset(QString::fromUtf8("Moonshot / Kimi"), QString::fromUtf8("Kimi K2"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.moonshot.ai/v1"),
                       QString::fromUtf8("kimi-k2-turbo-preview"));
    list << makePreset(QString::fromUtf8("Moonshot / Kimi"), QString::fromUtf8("Moonshot v1 128k"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.moonshot.cn/v1"),
                       QString::fromUtf8("moonshot-v1-128k"));

    list << makePreset(QString::fromUtf8("Zhipu / GLM"), QString::fromUtf8("GLM-4 Plus"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://open.bigmodel.cn/api/paas/v4"),
                       QString::fromUtf8("glm-4-plus"));
    list << makePreset(QString::fromUtf8("Zhipu / GLM"), QString::fromUtf8("GLM-4 Flash"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://open.bigmodel.cn/api/paas/v4"),
                       QString::fromUtf8("glm-4-flash"));

    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Auto Router"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("openrouter/auto"),
                       QString(), QString(),
                       QString::fromUtf8(
                           "OpenRouter auto-router: pick model id from the site if needed"));
    list << makePreset(QString::fromUtf8("OpenRouter"),
                       QString::fromUtf8("Claude Sonnet (via OR)"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("anthropic/claude-sonnet-4"));
    list << makePreset(QString::fromUtf8("OpenRouter"),
                       QString::fromUtf8("Claude Opus (via OR)"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("anthropic/claude-opus-4"));

    list << makePreset(QString::fromUtf8("Together AI"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.together.xyz/v1"),
                       QString::fromUtf8("meta-llama/Llama-3.3-70B-Instruct-Turbo"));
    list << makePreset(QString::fromUtf8("Fireworks"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.fireworks.ai/inference/v1"),
                       QString::fromUtf8("accounts/fireworks/models/llama-v3p3-70b-instruct"));
    list << makePreset(QString::fromUtf8("DeepInfra"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.deepinfra.com/v1/openai"),
                       QString::fromUtf8("meta-llama/Llama-3.3-70B-Instruct"));
    list << makePreset(QString::fromUtf8("Cerebras"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.cerebras.ai/v1"),
                       QString::fromUtf8("llama-3.3-70b"));
    list << makePreset(QString::fromUtf8("SambaNova"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.sambanova.ai/v1"),
                       QString::fromUtf8("Meta-Llama-3.3-70B-Instruct"));
    list << makePreset(QString::fromUtf8("SiliconFlow"), QString::fromUtf8("DeepSeek V3"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.siliconflow.cn/v1"),
                       QString::fromUtf8("deepseek-ai/DeepSeek-V3"));
    list << makePreset(QString::fromUtf8("Perplexity"), QString::fromUtf8("Sonar"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.perplexity.ai"),
                       QString::fromUtf8("sonar"));
    list << makePreset(QString::fromUtf8("Perplexity"), QString::fromUtf8("Sonar Pro"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.perplexity.ai"),
                       QString::fromUtf8("sonar-pro"));
    list << makePreset(QString::fromUtf8("Cohere"), QString::fromUtf8("Command R+"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.cohere.ai/compatibility/v1"),
                       QString::fromUtf8("command-r-plus"));
    list << makePreset(QString::fromUtf8("NVIDIA NIM"), QString::fromUtf8("Llama 3.3 70B"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://integrate.api.nvidia.com/v1"),
                       QString::fromUtf8("meta/llama-3.3-70b-instruct"));
    list << makePreset(QString::fromUtf8("GitHub Models"), QString::fromUtf8("GPT-4o"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://models.inference.ai.azure.com"),
                       QString::fromUtf8("gpt-4o"));
    list << makePreset(QString::fromUtf8("Azure OpenAI"), QString::fromUtf8("Deployment"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://YOUR_RESOURCE.openai.azure.com/openai/v1"),
                       QString::fromUtf8("gpt-4o"),
                       QString(), QString(),
                       QObject::tr("Подставьте свой resource и имя deployment."));
    list << makePreset(QString::fromUtf8("Cloudflare"), QString::fromUtf8("Workers AI"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8(
                           "https://api.cloudflare.com/client/v4/accounts/ACCOUNT_ID/ai/v1"),
                       QString::fromUtf8("@cf/meta/llama-3.1-70b-instruct"),
                       QString(), QString(),
                       QObject::tr("Подставьте ACCOUNT_ID."));
    list << makePreset(QString::fromUtf8("Hugging Face"), QString::fromUtf8("Router"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://router.huggingface.co/v1"),
                       QString::fromUtf8("meta-llama/Llama-3.3-70B-Instruct"));
    list << makePreset(QString::fromUtf8("MiniMax"), QString::fromUtf8("MiniMax Text"),
                       WorkerTaskKindLlm,
                       QString::fromUtf8("https://api.minimax.chat/v1"),
                       QString::fromUtf8("MiniMax-Text-01"));
    list << makePreset(QString::fromUtf8("Yi / 01.AI"), QString::fromUtf8("Yi Lightning"),
                       WorkerTaskKindLlm, QString::fromUtf8("https://api.lingyiwanwu.com/v1"),
                       QString::fromUtf8("yi-lightning"));

    list << makePreset(QString::fromUtf8("Ollama"), QString::fromUtf8("Local (llama)"),
                       WorkerTaskKindLlm, QString::fromUtf8("http://127.0.0.1:11434/v1"),
                       QString::fromUtf8("llama3.2"),
                       QString(), QString(),
                       QString::fromUtf8(
                           "Local LLM via Ollama (token usually unused)"));
    list << makePreset(QString::fromUtf8("LM Studio"), QString::fromUtf8("Local OpenAI"),
                       WorkerTaskKindLlm, QString::fromUtf8("http://127.0.0.1:1234/v1"),
                       QString::fromUtf8("local-model"));
    list << makePreset(QString::fromUtf8("vLLM"), QString::fromUtf8("Local OpenAI"),
                       WorkerTaskKindLlm, QString::fromUtf8("http://127.0.0.1:8000/v1"),
                       QString::fromUtf8("your-model"));

    // --- Image generation ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("GPT Image / DALL·E"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-image-1"),
                       QString(), QString(),
                       QString::fromUtf8("OpenAI Images API: text-to-image generation"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("DALL·E 3"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("dall-e-3"));
    list << makePreset(QString::fromUtf8("Together AI"), QString::fromUtf8("FLUX.1 Schnell"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.together.xyz/v1"),
                       QString::fromUtf8("black-forest-labs/FLUX.1-schnell"));
    list << makePreset(QString::fromUtf8("Together AI"), QString::fromUtf8("FLUX.1 Dev"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.together.xyz/v1"),
                       QString::fromUtf8("black-forest-labs/FLUX.1-dev"));
    list << makePreset(QString::fromUtf8("Fireworks"), QString::fromUtf8("FLUX Schnell"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.fireworks.ai/inference/v1"),
                       QString::fromUtf8("accounts/fireworks/models/flux-1-schnell-fp8"));
    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("Wanx / Qwen VL gen"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("wanx-v1"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));
    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Flux (via OR)"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("black-forest-labs/flux-1-schnell"));
    list << makePreset(QString::fromUtf8("SiliconFlow"), QString::fromUtf8("FLUX Schnell"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.siliconflow.cn/v1"),
                       QString::fromUtf8("black-forest-labs/FLUX.1-schnell"));
    list << makePreset(QString::fromUtf8("Stability"), QString::fromUtf8("Stable Image"),
                       WorkerTaskKindImageGeneration,
                       QString::fromUtf8("https://api.stability.ai/v2beta"),
                       QString::fromUtf8("stable-image-core"),
                       QString(), QString(),
                       QString::fromUtf8(
                           "Stability text-to-image (check Stability docs for API shape)"));

    // --- Speech ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("Whisper"),
                       WorkerTaskKindSpeech, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("whisper-1"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("TTS"),
                       WorkerTaskKindSpeech, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-4o-mini-tts"));
    list << makePreset(QString::fromUtf8("Groq"), QString::fromUtf8("Whisper Large v3"),
                       WorkerTaskKindSpeech,
                       QString::fromUtf8("https://api.groq.com/openai/v1"),
                       QString::fromUtf8("whisper-large-v3"));
    list << makePreset(QString::fromUtf8("Groq"), QString::fromUtf8("Whisper Large v3 Turbo"),
                       WorkerTaskKindSpeech,
                       QString::fromUtf8("https://api.groq.com/openai/v1"),
                       QString::fromUtf8("whisper-large-v3-turbo"));
    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("Qwen Audio / TTS"),
                       WorkerTaskKindSpeech,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("qwen-audio-turbo"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));
    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Whisper (via OR)"),
                       WorkerTaskKindSpeech, QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("openai/whisper-large-v3"));

    // --- Audio generation (music / SFX; not TTS) ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("Audio gen (compat)"),
                       WorkerTaskKindAudioGeneration,
                       QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-audio"),
                       QString(), QString(),
                       QObject::tr("Проверьте endpoint /audio/generations у провайдера."));
    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Audio gen (via OR)"),
                       WorkerTaskKindAudioGeneration,
                       QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("openai/gpt-audio"),
                       QString(), QString(),
                       QObject::tr("Зависит от поддержки провайдера на OpenRouter."));

    // --- Video generation ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("Sora 2"),
                       WorkerTaskKindVideoGeneration,
                       QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("sora-2"),
                       QString(), QString(),
                       QObject::tr("Часто async job; ответ может быть id/status."));
    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Video gen (via OR)"),
                       WorkerTaskKindVideoGeneration,
                       QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("openai/sora"),
                       QString(), QString(),
                       QObject::tr("Уточните model id в каталоге OpenRouter."));

    // --- Decisions (LLM chat + propose_decision) ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("GPT-4.1 mini (decisions)"),
                       WorkerTaskKindDecisions, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("gpt-4.1-mini"));
    list << makePreset(QString::fromUtf8("DeepSeek"), QString::fromUtf8("DeepSeek Chat (decisions)"),
                       WorkerTaskKindDecisions, QString::fromUtf8("https://api.deepseek.com"),
                       QString::fromUtf8("deepseek-chat"));
    list << makePreset(QString::fromUtf8("OpenRouter"), QString::fromUtf8("Claude Haiku (decisions)"),
                       WorkerTaskKindDecisions, QString::fromUtf8("https://openrouter.ai/api/v1"),
                       QString::fromUtf8("anthropic/claude-3.5-haiku"));

    // --- Embeddings ---
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("text-embedding-3-large"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("text-embedding-3-large"));
    list << makePreset(QString::fromUtf8("OpenAI"), QString::fromUtf8("text-embedding-3-small"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.openai.com/v1"),
                       QString::fromUtf8("text-embedding-3-small"));
    list << makePreset(QString::fromUtf8("Voyage"), QString::fromUtf8("voyage-3"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.voyageai.com/v1"),
                       QString::fromUtf8("voyage-3"));
    list << makePreset(QString::fromUtf8("Cohere"), QString::fromUtf8("embed-v4"),
                       WorkerTaskKindEmbedding,
                       QString::fromUtf8("https://api.cohere.ai/compatibility/v1"),
                       QString::fromUtf8("embed-v4.0"));
    list << makePreset(QString::fromUtf8("Qwen / DashScope"), QString::fromUtf8("text-embedding-v3"),
                       WorkerTaskKindEmbedding,
                       QString::fromUtf8(
                           "https://dashscope-intl.aliyuncs.com/compatible-mode/v1"),
                       QString::fromUtf8("text-embedding-v3"),
                       QString::fromUtf8("/api/v1/quotas"),
                       QString::fromUtf8("data.credits"));
    list << makePreset(QString::fromUtf8("Jina"), QString::fromUtf8("jina-embeddings-v3"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.jina.ai/v1"),
                       QString::fromUtf8("jina-embeddings-v3"));
    list << makePreset(QString::fromUtf8("Ollama"), QString::fromUtf8("nomic-embed-text"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("http://127.0.0.1:11434/v1"),
                       QString::fromUtf8("nomic-embed-text"));
    list << makePreset(QString::fromUtf8("SiliconFlow"), QString::fromUtf8("BGE-M3"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.siliconflow.cn/v1"),
                       QString::fromUtf8("BAAI/bge-m3"));
    list << makePreset(QString::fromUtf8("Mistral"), QString::fromUtf8("mistral-embed"),
                       WorkerTaskKindEmbedding, QString::fromUtf8("https://api.mistral.ai/v1"),
                       QString::fromUtf8("mistral-embed"));

    // Default purpose by task class when none was set explicitly.
    for (int i = 0; i < list.size(); ++i)
    {
        if (!list[i].purpose.trimmed().isEmpty())
            continue;
        switch (list[i].modality)
        {
        case WorkerTaskKindImageGeneration:
            list[i].purpose = QString::fromUtf8(
                "Text-to-image generation (not object detection/segmentation)");
            break;
        case WorkerTaskKindSpeech:
            list[i].purpose = QString::fromUtf8("Speech: TTS and/or STT");
            break;
        case WorkerTaskKindAudioGeneration:
            list[i].purpose = QString::fromUtf8("Generate music / sound effects from text");
            break;
        case WorkerTaskKindVideoGeneration:
            list[i].purpose = QString::fromUtf8("Text/image-to-video generation");
            break;
        case WorkerTaskKindEmbedding:
            list[i].purpose = QString::fromUtf8("Text embeddings / vector search");
            break;
        case WorkerTaskKindDecisions:
            list[i].purpose = QString::fromUtf8("Structured decisions among options");
            break;
        case WorkerTaskKindLlm:
        case WorkerTaskKindOther:
        default:
            list[i].purpose = QString::fromUtf8(
                "General LLM: coding, reasoning, agent tool use");
            break;
        }
    }

    // Refinements for known specializations inside one modality.
    for (int i = 0; i < list.size(); ++i)
    {
        const QString m = list[i].model.toLower();
        const QString n = list[i].name.toLower();
        if (m.contains(QString::fromUtf8("whisper"))
            || n.contains(QString::fromUtf8("whisper")))
            list[i].purpose = QString::fromUtf8("Speech-to-text transcription (ASR/STT)");
        else if (m.contains(QString::fromUtf8("tts"))
                 || n.contains(QString::fromUtf8("tts")))
            list[i].purpose = QString::fromUtf8("Text-to-speech synthesis (TTS)");
        else if (m.contains(QString::fromUtf8("reasoner"))
                 || m.startsWith(QString::fromUtf8("o3"))
                 || m.startsWith(QString::fromUtf8("o4")))
            list[i].purpose = QString::fromUtf8(
                "Deep reasoning / hard bugs / ambiguous design");
        else if (m.contains(QString::fromUtf8("codestral"))
                 || n.contains(QString::fromUtf8("codestral")))
            list[i].purpose = QString::fromUtf8("Code generation and editing");
        else if (m.contains(QString::fromUtf8("sonar")))
            list[i].purpose = QString::fromUtf8("Web-grounded search answers");
        else if (m.contains(QString::fromUtf8("-vl"))
                 || m.contains(QString::fromUtf8("vision"))
                 || n.contains(QString::fromUtf8(" vl")))
            list[i].purpose = QString::fromUtf8(
                "Vision LLM: describe/analyze images (not image generation)");
        else if (list[i].modality == WorkerTaskKindImageGeneration
                 && (m.contains(QString::fromUtf8("edit"))
                     || m.contains(QString::fromUtf8("layer"))
                     || n.contains(QString::fromUtf8("edit"))))
            list[i].purpose = QString::fromUtf8(
                "Image editing / inpainting with a reference image");
    }

    return list;
}

//----------------------------------------------

QString modelProfilePresetLabel(const ModelProfilePreset &preset)
{
    const QString modality = workerTaskKindToString(preset.modality);
    QString label = preset.provider.trimmed();
    if (!preset.name.trimmed().isEmpty())
        label += QString::fromUtf8(" · ") + preset.name.trimmed();
    label += QString::fromUtf8(" · ") + modality;
    if (!preset.model.trimmed().isEmpty())
        label += QString::fromUtf8(" · ") + preset.model.trimmed();
    return label;
}
