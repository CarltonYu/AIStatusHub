use axum::{http::HeaderMap, response::IntoResponse, Json};
use serde_json::json;

pub fn bearer_token(headers: &HeaderMap) -> Option<&str> {
    headers
        .get(axum::http::header::AUTHORIZATION)
        .and_then(|value| value.to_str().ok())
        .and_then(|value| value.strip_prefix("Bearer "))
}

pub fn require_token(
    headers: &HeaderMap,
    expected: Option<&str>,
) -> Result<(), axum::response::Response> {
    let Some(expected) = expected else {
        return Ok(());
    };

    if bearer_token(headers) == Some(expected) {
        Ok(())
    } else {
        Err((
            axum::http::StatusCode::UNAUTHORIZED,
            Json(json!({ "error": "unauthorized" })),
        )
            .into_response())
    }
}
