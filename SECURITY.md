# Guia de Segurança

## Informações Sensíveis

Este projeto requer credenciais sensíveis que **NUNCA** devem ser commitadas no repositório:

### Arquivos com Dados Sensíveis (já no .gitignore)

- `sdkconfig` - Contém WiFi password, MQTT password
- `sdkconfig.old` - Backup do sdkconfig
- `.env` - Variáveis de ambiente locais
- `build/` - Pode conter artefatos com credenciais compiladas

### Como Configurar Credenciais com Segurança

#### Opção 1: Menuconfig (Padrão ESP-IDF)
```bash
idf.py menuconfig
```
Configure suas credenciais em "Component config" → "Project Configuration"

#### Opção 2: Arquivo Local (Não commitado)
```bash
cp sdkconfig.defaults sdkconfig.defaults.local
# Edite sdkconfig.defaults.local com suas credenciais
```

## Checklist Antes de Fazer Push

✅ **SEMPRE verifique antes de fazer git push:**

```bash
# 1. Verifique o status
git status

# 2. Certifique-se de que estes arquivos NÃO aparecem:
#    - sdkconfig
#    - sdkconfig.old
#    - .env
#    - arquivos em build/

# 3. Verifique o que será commitado
git diff --cached

# 4. Se encontrar credenciais, remova imediatamente:
git reset HEAD <arquivo_sensível>
```

## O Que Fazer Se Acidentalmente Commitar Credenciais

Se você acidentalmente commitou credenciais:

### 1. Se ainda NÃO fez push:
```bash
# Desfazer o último commit mantendo as mudanças
git reset --soft HEAD~1

# Remover arquivo do staging
git reset HEAD sdkconfig

# Fazer novo commit sem o arquivo sensível
git add .
git commit -m "Seu commit sem credenciais"
```

### 2. Se JÁ fez push:

⚠️ **AÇÃO URGENTE NECESSÁRIA:**

1. **Trocar IMEDIATAMENTE todas as credenciais expostas:**
   - Senha WiFi
   - Senha MQTT
   - Qualquer outro secret

2. **Limpar o histórico do Git:**
```bash
# Usar git filter-branch ou BFG Repo Cleaner
git filter-branch --force --index-filter \
  "git rm --cached --ignore-unmatch sdkconfig" \
  --prune-empty --tag-name-filter cat -- --all

# Force push (cuidado!)
git push origin --force --all
```

3. **Considere recriar o repositório** se as credenciais forem críticas

## Práticas Recomendadas

- ✅ Use senhas fortes e únicas
- ✅ Revise o diff antes de cada commit
- ✅ Configure o Git para ignorar arquivos sensíveis globalmente
- ✅ Use git hooks para prevenir commits acidentais
- ✅ Mantenha o `.gitignore` atualizado
- ❌ Nunca commite arquivos de configuração com credenciais reais
- ❌ Nunca faça push de `sdkconfig` ou `sdkconfig.old`

## Git Hooks (Opcional)

Crie um pre-commit hook para verificar automaticamente:

```bash
#!/bin/bash
# .git/hooks/pre-commit

if git diff --cached --name-only | grep -E "sdkconfig|\.env"; then
    echo "❌ ERRO: Tentativa de commitar arquivo sensível detectada!"
    echo "Arquivos bloqueados: sdkconfig, .env"
    exit 1
fi
```

Torne o hook executável:
```bash
chmod +x .git/hooks/pre-commit
```

## Suporte

Se você tiver dúvidas sobre segurança ou acidentalmente expor credenciais, abra uma issue (sem incluir as credenciais!) ou entre em contato com o mantenedor.
