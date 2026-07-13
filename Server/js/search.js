let input = document.getElementById("search-input");
let keyword = "";
let keywords = [];
let result = [];
function printNewsList(result){
    let html="";
    result.forEach(news=>{
        html +=
        `
        <div class="news-card">
        <div class="news-source"> ${highlight(news.source, keyword)} </div>
        <div class="news-title"> ${highlight(news.title, keyword)} </div>
        <div class="news-time"> ${highlight(news.time, keyword)} </div>
        <div class="news-abstract"> ${highlight(news.abstract, keyword)} </div>
        <a class="detail-btn" href="detail/${news.id}.html?from=search">查看详情</a>
        </div>
        `;
    });
    document.getElementById("news-list").innerHTML = html;
}
function escapeRegExp(string){

    return string.replace(/[.*+?^${}()|[\]\\]/g,'\\$&');

}
function highlight(text, keyword){

    if(!text){
        return text;
    }
    if(keyword==""){
        return text;
    }
    let result = text;
    keywords.forEach(key => {
        if(key.trim() !== ""){
            let reg = new RegExp(escapeRegExp(key), "g");
            result = result.replace(reg, `<span class='highlight'>${key}</span>`);
        }
    });
    return result;

}
function search(newsList){
    let params = new URLSearchParams(window.location.search);
    keyword = input.value.trim() || params.get("keyword") || "";
    keywords = keyword.trim().split(/\s+/).filter(key=>key.length>0);
    result = newsList.filter(news=>{
        return keywords.every(key=>{
            return(
            (news.title && news.title.includes(key))
            ||  
            (news.time && news.time.includes(key))
            ||
            (news.abstract && news.abstract.includes(key))
            ||
            (news.content && news.content.includes(key))
            ||
            (news.source && news.source.includes(key))
            ||
            (news.topic && news.topic.includes(key))
            );
        });
        
    });
    printNewsList(result);
    document.getElementById("search-keyword").innerHTML = keyword;
}

let newsList = [];
fetch("json/news.json")
.then(response => response.json())
.then(data => {
    newsList = data;
    
    let button = document.getElementById("search-button");
    button.addEventListener("click", function(){
        search(newsList);
    });
    search(newsList);

    let sortDesc = document.getElementById("sort-desc");
    sortDesc.addEventListener("click", function(){
        result.sort(function(a,b){ return new Date(b.time)-new Date(a.time); });
        printNewsList(result);
    });

    let sortAsc = document.getElementById("sort-asc");
    sortAsc.addEventListener("click", function(){
        result.sort(function(a,b){ return new Date(a.time)-new Date(b.time); });
        printNewsList(result);
    });
})
