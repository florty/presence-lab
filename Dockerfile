FROM python:3.12-slim
RUN pip install --no-cache-dir "aioesphomeapi==46.28.0" "aiohttp>=3.9,<4"
WORKDIR /app
COPY bridge/server.py .
CMD ["python", "-u", "server.py"]
